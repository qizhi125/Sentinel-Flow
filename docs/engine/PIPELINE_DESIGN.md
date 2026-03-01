# 解析流水线设计

## 概述

`PacketPipeline` 是 Sentinel-Flow 数据面的核心处理单元，每个实例运行在独立线程中，负责从 SPSC 队列获取原始报文，执行协议解析、安全检测，并将结果批量投递给 UI 层。通过 CPU 亲和性绑定、批处理缓冲和背压感知，实现在高吞吐环境下的稳定处理。

## 类设计

```cpp
    class PacketPipeline : public QThread {
        Q_OBJECT
    public:
        explicit PacketPipeline(QObject *parent = nullptr);
        ~PacketPipeline() override;
    
        void setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue);
        void setInspector(sentinel::engine::IInspector* inspector);
        void setCoreId(int coreId);
        void startPipeline();
        void stopPipeline();
    
    signals:
        void packetsProcessed(QSharedPointer<QVector<ParsedPacket>> packets);
        void threatDetected(const Alert& alert, const ParsedPacket& packet);
        void statsUpdated(uint64_t bytesProcessed);
    
    protected:
        void run() override;
    
    private:
        sentinel::common::SPSCQueue<RawPacket>* inputQueue = nullptr;
        sentinel::engine::IInspector* m_inspector = nullptr;
        std::atomic<bool> running{false};
        int m_coreId = -1;
        QVector<ParsedPacket> packetBatch;
    };
```

## 线程生命周期

### 启动

- `startPipeline()` 设置 `running = true`，调用 `start(QThread::HighPriority)` 启动线程。
- 线程入口 `run()` 循环处理报文，直至 `running` 被置为 `false`。

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
        // 2. 初始化统计变量
        uint64_t bytesAccumulator = 0;
        QElapsedTimer timer;
        timer.start();
    
        while (running) {
            // 3. 从队列获取报文（超时 100ms）
            auto rawOpt = inputQueue->popWait(std::chrono::milliseconds(100));
    
            // 4. 定期批量投递 UI 数据
            if (timer.elapsed() > UI_REFRESH_INTERVAL_MS || packetBatch.size() >= BATCH_RESERVE_SIZE) {
                if (!packetBatch.isEmpty()) {
                    emit packetsProcessed(QSharedPointer<QVector<ParsedPacket>>::create(std::move(packetBatch)));
                    packetBatch.clear();
                    packetBatch.reserve(BATCH_RESERVE_SIZE);
                }
                if (bytesAccumulator > 0) {
                    emit statsUpdated(bytesAccumulator);
                    bytesAccumulator = 0;
                }
                timer.restart();
            }
    
            // 5. 若未获取到报文，继续循环
            if (!rawOpt) continue;
    
            // 6. 统计字节数
            RawPacket& raw = *rawOpt;
            bytesAccumulator += raw.block ? raw.block->size : 0;
    
            // 7. 解析报文
            auto parsedOpt = PacketParser::parse(raw);
            if (!parsedOpt) continue;
            ParsedPacket& parsed = *parsedOpt;
    
            // 8. 黑名单过滤
            if (SecurityEngine::instance().isIpBlocked(parsed.srcIp) ||
                SecurityEngine::instance().isIpBlocked(parsed.dstIp)) {
                continue;
            }
    
            // 9. 威胁检测
            if (m_inspector) {
                auto alertOpt = m_inspector->inspect(parsed);
                if (alertOpt) {
                    DatabaseManager::instance().saveAlert(*alertOpt);
                    emit threatDetected(*alertOpt, parsed);
                }
            }
    
            // 10. 释放数据块引用（减少引用计数）
            parsed.block.reset();
    
            // 11. 加入批处理缓冲区
            packetBatch.append(std::move(parsed));
        }
    }
```

## 批处理机制

### 双条件触发

- **数量阈值**：当 `packetBatch.size() >= 5000` 时，立即投递。
- **时间阈值**：当距离上次投递超过 150ms 时，即使数量未满也投递。

### 批量投递优势

- 减少跨线程信号次数，降低 Qt 事件循环压力。
- 使用 `QSharedPointer` 避免数据拷贝，仅传递指针。
- 投递后清空缓冲区并预分配容量，避免多次内存重分配。

### 统计信息批处理

- `bytesAccumulator` 累计处理的字节数，随批量投递一并发出，用于 UI 显示实时速率。

## 错误处理与异常安全

- `run()` 中的核心循环使用 `try-catch` 捕获 `std::exception` 和 `...`，防止单个报文处理异常导致线程崩溃。
- 异常捕获后仅打印错误，继续处理下一个报文。

## 组件交互

### 输入源

- 通过 `setInputQueue()` 绑定一个 SPSC 队列，由 `PcapCapture` 填充数据。

### 检测引擎

- 通过 `setInspector()` 注入 `SecurityEngine` 实例（实现 `IInspector` 接口）。

### 输出信号

- `packetsProcessed`：发送给 UI 的 `TrafficTableModel`，用于实时流量展示。
- `threatDetected`：发送给 `AlertsPage` 和 `DashboardPage`，用于告警展示和雷达触发。
- `statsUpdated`：发送给 `StatisticsPage`，用于更新吞吐量图表。

## 线程安全设计

- 所有成员变量均为单线程访问（仅在 `run()` 中使用），无需额外同步。
- 信号跨线程发送由 Qt 自动处理，线程安全。
- 输入队列 `SPSCQueue` 本身就是无锁的，支持并发读写。

## 性能调优参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `BATCH_RESERVE_SIZE` | 5000 | 批处理缓冲区预分配容量 |
| `UI_REFRESH_INTERVAL_MS` | 150 | 强制投递时间间隔（毫秒） |
| 队列超时 | 100 ms | `popWait` 等待时间，用于定期检查退出标志 |

## 使用示例

```cpp
    // 创建管线实例
    PacketPipeline* pipe = new PacketPipeline();
    
    // 绑定输入队列
    pipe->setInputQueue(&queue);
    
    // 设置检测引擎
    pipe->setInspector(&SecurityEngine::instance());
    
    // 绑定 CPU 核心（可选）
    pipe->setCoreId(2);
    
    // 连接信号
    connect(pipe, &PacketPipeline::packetsProcessed, model, &TrafficTableModel::addPackets);
    connect(pipe, &PacketPipeline::threatDetected, alertsPage, &AlertsPage::addAlert);
    
    // 启动管线
    pipe->startPipeline();
    
    // 停止管线
    pipe->stopPipeline();
    pipe->wait();
    delete pipe;
```

## 与同类设计的对比

| 特性 | 传统逐包处理 | Sentinel-Flow 批处理 |
|------|-------------|----------------------|
| 信号频率 | 每包一次 | 每批一次（最多 5000 包） |
| UI 刷新 | 逐包触发重绘 | 批量更新模型，UI 按需刷新 |
| 跨线程拷贝 | 每包拷贝数据 | 共享指针传递，零拷贝 |
| CPU 亲和性 | 未绑定 | 支持绑定，优化缓存 |

---
