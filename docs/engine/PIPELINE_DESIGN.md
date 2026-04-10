# 解析流水线设计

## 概述

`PacketPipeline` 是 Sentinel-Flow 数据面的核心处理单元，每个实例运行在独立线程中，负责从 SPSC 队列获取原始报文，执行协议解析、安全检测，并将结果通过回调函数批量投递给上层（Go 控制面或内部日志模块）。通过 CPU 亲和性绑定、批处理缓冲和背压感知，实现在高吞吐环境下的稳定处理。

## 类设计

```cpp
namespace sentinel::engine {

class PacketPipeline {
public:
    PacketPipeline();
    ~PacketPipeline();

    void setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue);
    void setInspector(sentinel::engine::IInspector* inspector);
    void setCoreId(int coreId);

    // 设置回调函数
    using BatchCallback = std::function<void(std::shared_ptr<std::vector<ParsedPacket>>)>;
    using ThreatCallback = std::function<void(const Alert&, const ParsedPacket&)>;
    using StatsCallback = std::function<void(uint64_t bytesProcessed)>;
    void setCallBack(BatchCallback batchCb, ThreatCallback threatCb, StatsCallback statsCb);

    void startPipeline();
    void stopPipeline();
    void wait();

private:
    void run();
    void flushBatch(uint64_t& bytesAccumulator);

    sentinel::common::SPSCQueue<RawPacket>* inputQueue = nullptr;
    sentinel::engine::IInspector* m_inspector = nullptr;
    std::atomic<bool> running{false};
    int m_coreId = -1;

    std::shared_ptr<std::vector<ParsedPacket>> currentBatch;
    std::thread workerThread;

    BatchCallback m_batchCb;
    ThreatCallback m_threatCb;
    StatsCallback m_statsCb;
};

} // namespace sentinel::engine
```

## 线程生命周期

### 启动

- `startPipeline()` 设置 `running = true`，创建 `std::thread` 并执行 `run()`。
- 线程创建后立即开始处理循环。

### 停止

- `stopPipeline()` 将 `running` 设为 `false`。
- `run()` 中的 `popWait` 会在超时后检查标志，退出循环。
- 析构函数调用 `stopPipeline()` 并 `wait()` 等待线程结束。

## CPU 亲和性绑定

```cpp
if (m_coreId >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(m_coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}
```

- 通过 `setCoreId()` 指定目标 CPU 核心。
- 在 `run()` 开始时调用 `pthread_setaffinity_np` 绑定线程到指定核心，减少上下文切换，提高缓存局部性。

## 核心处理循环

```cpp
void PacketPipeline::run() {
    // 1. 绑定 CPU 核心（可选）
    if (m_coreId >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(m_coreId, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    uint64_t bytesAccumulator = 0;
    auto lastFlushTime = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_acquire)) {
        try {
            // 2. 从队列获取报文（超时 100ms）
            auto rawOpt = inputQueue->popWait(std::chrono::milliseconds(100));

            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime).count();

            // 3. 定期批量触发回调
            if (elapsedMs > UI_REFRESH_INTERVAL_MS || currentBatch->size() >= BATCH_RESERVE_SIZE) {
                flushBatch(bytesAccumulator);
                lastFlushTime = std::chrono::steady_clock::now();
            }

            if (!rawOpt) continue;

            // 4. 统计字节数
            RawPacket& raw = *rawOpt;
            bytesAccumulator += raw.block ? raw.block->size : 0;

            // 5. 解析报文
            auto parsedOpt = PacketParser::parse(raw);
            if (!parsedOpt) continue;
            ParsedPacket& parsed = *parsedOpt;

            // 6. 黑名单过滤
            if (SecurityEngine::instance().isIpBlocked(parsed.srcIp) ||
                SecurityEngine::instance().isIpBlocked(parsed.dstIp)) {
                continue;
            }

            // 7. 威胁检测
            if (m_inspector) {
                auto alertOpt = m_inspector->inspect(parsed);
                if (alertOpt) {
                    DatabaseManager::instance().saveAlert(*alertOpt);
                    if (m_threatCb) {
                        m_threatCb(*alertOpt, parsed);
                    }
                }
            }

            // 8. 释放数据块引用（减少引用计数）
            parsed.block.reset();

            // 9. 加入批处理缓冲区
            currentBatch->emplace_back(std::move(parsed));
        } catch (const std::exception& e) {
            std::cerr << "[!] Pipeline Worker Exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[!] Unknown Exception in Pipeline Worker!" << std::endl;
        }
    }
    flushBatch(bytesAccumulator);
}
```

## 批处理机制

### 双条件触发

- **数量阈值**：当 `currentBatch->size() >= 5000` 时，立即刷新。
- **时间阈值**：当距离上次刷新超过 150ms 时，即使数量未满也刷新。

### 批量投递优势

- 减少回调调用频率，降低上层（如 Go 控制面）的处理压力。
- 使用 `std::shared_ptr` 传递批量数据，避免拷贝，仅传递指针。
- 刷新后重新分配新的缓冲区，旧缓冲区生命周期由接收方管理，实现零拷贝数据移交。

### 统计信息批处理

- `bytesAccumulator` 累计处理的字节数，随批量刷新一并发出，用于计算实时吞吐量。

## 回调函数绑定

在 `capi_impl.cpp` 中，引擎启动时为每个 `PacketPipeline` 绑定回调：

```cpp
pipeline->setCallBack(
    nullptr,  // 批量报文回调（当前未使用）
    [ctx](const Alert& a, const ParsedPacket& p) {
        // 威胁告警回调 → 转换为 C 结构并调用 Go 注册的回调
        if (ctx->alert_cb) {
            AlertEvent ev{};
            ev.timestamp_ns = a.timestamp * 1000000ULL;
            ev.src_ip = a.sourceIp;
            ev.dst_ip = p.dstIp;
            // ... 填充其他字段
            ctx->alert_cb(&ev, ctx->user_data);
        }
    },
    [ctx](uint64_t bytesAccumulator) {
        // 统计回调 → 透传字节累计值供 Go 计算吞吐量
        if (ctx->stats_cb) {
            EngineStats stats{};
            stats.current_qps = bytesAccumulator;
            ctx->stats_cb(&stats, ctx->user_data);
        }
    }
);
```

## 错误处理与异常安全

- `run()` 中的核心循环使用 `try-catch` 捕获 `std::exception` 和 `...`，防止单个报文处理异常导致线程崩溃。
- 异常捕获后仅打印错误，继续处理下一个报文。

## 组件交互

### 输入源

- 通过 `setInputQueue()` 绑定一个 SPSC 队列，由捕获驱动（`PcapCapture` 或 `EBPFCapture`）填充数据。

### 检测引擎

- 通过 `setInspector()` 注入 `SecurityEngine` 实例（实现 `IInspector` 接口）。

### 输出回调

- `BatchCallback`：预留用于批量投递解析后的报文（当前未使用）。
- `ThreatCallback`：发现威胁时触发，传递给 C API 层并最终调用 Go 侧回调。
- `StatsCallback`：定期触发，上报处理字节数，用于实时吞吐量显示。

## 线程安全设计

- 所有成员变量均为单线程访问（仅在 `run()` 中使用），无需额外同步。
- 输入队列 `SPSCQueue` 自身是无锁的，支持并发读写。
- 回调函数在管线线程中同步执行，若回调处理耗时可能影响管线吞吐，建议回调内部快速返回或将耗时操作异步化。

## 性能调优参数

| 参数                     | 默认值 | 说明                                     |
| ------------------------ | ------ | ---------------------------------------- |
| `BATCH_RESERVE_SIZE`     | 5000   | 批处理缓冲区预分配容量                   |
| `UI_REFRESH_INTERVAL_MS` | 150    | 强制刷新时间间隔（毫秒）                 |
| 队列超时                 | 100 ms | `popWait` 等待时间，用于定期检查退出标志 |

## 使用示例

```cpp
// 创建管线实例
auto pipeline = std::make_unique<sentinel::engine::PacketPipeline>();

// 绑定输入队列
pipeline->setInputQueue(&queue);

// 设置检测引擎
pipeline->setInspector(&SecurityEngine::instance());

// 绑定 CPU 核心（可选）
pipeline->setCoreId(2);

// 设置回调
pipeline->setCallBack(
    nullptr,
    [](const Alert& a, const ParsedPacket& p) {
        // 处理告警
    },
    [](uint64_t bytes) {
        // 处理统计
    }
);

// 启动管线
pipeline->startPipeline();

// 停止管线
pipeline->stopPipeline();
pipeline->wait();
```

## 与旧版（Qt/QThread）的主要差异

| 特性         | 旧版（Qt GUI）                          | 当前版本（Go CLI）                           |
| ------------ | --------------------------------------- | -------------------------------------------- |
| 线程基类     | `QThread`                               | `std::thread`                                |
| 跨线程通信   | Qt 信号/槽 (`emit packetsProcessed`)    | 回调函数 (`std::function`)                   |
| 批量数据传递 | `QSharedPointer<QVector<ParsedPacket>>` | `std::shared_ptr<std::vector<ParsedPacket>>` |
| 统计上报     | Qt 信号 `statsUpdated`                  | 回调函数 `StatsCallback`                     |
| CPU 亲和性   | 同左，通过 `pthread_setaffinity_np`     | 同左                                         |
| 核心处理逻辑 | 基本相同                                | 基本相同                                     |

