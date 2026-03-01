# 取证文件生成机制

## 概述

`ForensicWorker` 是 Sentinel-Flow 的离线取证组件，负责将检测到的高危告警对应的原始数据包保存为独立的 PCAP 文件。通过异步批量写入机制，在保障数据面性能的同时，为安全分析人员提供完整的网络证据留存能力。

## 设计目标

- **自动取证**：当检测到高危告警（`level >= High`）时，自动保存相关数据包。
- **性能无侵扰**：写入操作在独立线程中异步执行，不阻塞解析管线。
- **批量聚合**：将多个数据包合并写入同一文件，减少磁盘 I/O 次数。
- **可追溯性**：文件命名包含时间戳和数据包数量，便于检索。

## 实现原理

### 类定义

```cpp
    class ForensicWorker {
    public:
        ForensicWorker();
        ~ForensicWorker();
    
        void start();
        void stop();
        void enqueue(const ParsedPacket& pkt);
    
    private:
        void run();
        void saveBatchToPcap(const std::vector<ParsedPacket>& batch);
    
        std::vector<ParsedPacket> packetBuffer_;
        std::mutex bufferMutex_;
        std::condition_variable cv_;
        std::thread workerThread_;
        std::atomic<bool> running_{false};
    };
```

### 触发机制

在 `SecurityEngine::inspect()` 中，若告警等级 >= `High`，调用：

```cpp
    forensicWorker_.enqueue(packet);
```

### 异步缓冲区

- **缓冲队列**：`packetBuffer_` 保存待写入的 `ParsedPacket`。
- **互斥保护**：`bufferMutex_` 保护缓冲区，`cv_` 用于唤醒工作线程。
- **批量阈值**：当缓冲区大小达到 500 条，或超过 30 秒未写入，触发写入。

```cpp
    void ForensicWorker::enqueue(const ParsedPacket& pkt) {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        packetBuffer_.push_back(pkt);
        if (packetBuffer_.size() >= 500) {
            cv_.notify_one();
        }
    }
```

### 工作线程

工作线程在循环中等待条件变量，当缓冲区满或超时（30 秒）时，取出全部数据，调用 `saveBatchToPcap` 写入文件。

```cpp
    void ForensicWorker::run() {
        while (running_) {
            std::vector<ParsedPacket> localBatch;
            {
                std::unique_lock<std::mutex> lock(bufferMutex_);
                cv_.wait_for(lock, std::chrono::seconds(30), [this] {
                    return packetBuffer_.size() >= 500 || !running_;
                });
                if (packetBuffer_.empty()) continue;
                localBatch.swap(packetBuffer_);
            }
            if (!localBatch.empty()) {
                saveBatchToPcap(localBatch);
            }
        }
    }
```

### PCAP 文件写入

```cpp
    void ForensicWorker::saveBatchToPcap(const std::vector<ParsedPacket>& batch) {
        QString path = QString("evidences/batch_%1_count_%2.pcap")
                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"))
                        .arg(batch.size());
    
        pcap_t* dead = pcap_open_dead(DLT_EN10MB, 65535);
        pcap_dumper_t* dumper = pcap_dump_open(dead, path.toLocal8Bit().constData());
    
        for (const auto& pkt : batch) {
            struct pcap_pkthdr h;
            h.ts.tv_sec = pkt.timestamp / 1000;
            h.ts.tv_usec = (pkt.timestamp % 1000) * 1000;
            h.caplen = pkt.totalLen;
            h.len = pkt.totalLen;
    
            if (pkt.block) {
                pcap_dump((u_char*)dumper, &h, pkt.block->data);
            }
        }
    
        pcap_dump_close(dumper);
        pcap_close(dead);
    }
```

- 使用 `pcap_open_dead` 创建虚拟 `pcap_t`，适用于仅写入文件。
- `pcap_dump_open` 创建 dumper，以 `DLT_EN10MB`（以太网）作为链路层类型。
- 将 `ParsedPacket` 中的原始数据（`block->data`）和元数据（时间戳、长度）写入。
- 文件保存路径：`evidences/batch_YYYYMMDD_HHMMSS_zzz_count_N.pcap`。

## 文件命名规范

- **前缀**：`batch_` 表示批处理文件。
- **时间戳**：精确到毫秒，`yyyyMMdd_HHmmss_zzz`。
- **数量**：`count_N` 表示该文件包含的数据包数量。
- **扩展名**：`.pcap`。

示例：`evidences/batch_20250324_143052_123_count_500.pcap`

## 线程安全与性能

- **独立线程**：取证写入完全在后台线程进行，不影响主解析流程。
- **锁粒度**：仅在访问缓冲区时加锁，写入过程不持有锁。
- **批量写入**：一次写入最多 500 个数据包，减少文件打开/关闭开销。
- **无内存拷贝**：`ParsedPacket` 中的 `block` 是 `shared_ptr`，写入时直接引用原始数据，无需额外拷贝。

## 配置与使用

### 启动与停止

```cpp
    ForensicWorker worker;
    worker.start();   // 启动工作线程
    worker.stop();    // 停止并等待线程结束
```

在 `SecurityEngine` 构造时自动启动，析构时停止。

### 数据包来源

仅当告警等级为 `High` 或 `Critical` 时，`SecurityEngine` 才会调用 `forensicWorker_.enqueue(packet)`，确保只保存最关键的威胁证据。

### 目录管理

- 默认目录：`evidences/`（相对于程序运行路径）。
- 系统自动创建目录（`QDir().mkpath("evidences")`）。
- 用户可在 `SettingsPage` 中修改取证保存路径（当前版本为预留功能）。

## 局限性

- **无自动清理**：长期运行可能导致 `evidences/` 目录占用大量磁盘空间，需用户定期手动清理。
- **仅支持以太网链路层**：`pcap_open_dead` 固定使用 `DLT_EN10MB`，不适用于其他链路类型（如 Linux SLL）。
- **不支持 PCAPNG**：输出为标准 PCAP 格式，不支持 PCAPNG 的高级特性。
- **数据包截断标记**：若原始数据包在捕获时已被截断（`isTruncated = true`），保存的 PCAP 文件仍为截断版本。

## 扩展建议

- **自动轮转**：可添加磁盘空间监控，当剩余空间低于阈值时自动删除最旧文件。
- **压缩存储**：写入后可选压缩为 `.pcap.gz`，节省空间。
- **加密存储**：为敏感环境添加 AES 加密支持。
- **元数据索引**：在 SQLite 中建立索引，便于按时间、规则快速定位取证文件。

---
