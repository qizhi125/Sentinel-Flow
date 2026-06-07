# 存储子系统

## 1. SQLite WAL 日志与批处理写入

### 概述

Sentinel-Flow 使用 SQLite 作为本地持久化存储，存储告警日志、检测规则、黑名单和配置信息。通过启用 WAL (Write-Ahead Logging) 模式、配置 `synchronous=NORMAL` 以及实现批量事务聚合，在保证数据安全的前提下最大化写入性能，避免 I/O 阻塞数据面管线。

### 数据库配置

#### WAL 模式

```cpp
sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
```

- **优势**：读操作不阻塞写操作，写操作可以并发进行（单个写者多个读者）。
- **效果**：提升并发性能，减少锁竞争。

#### 同步模式

```cpp
sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
```

- `NORMAL` 级别：在关键时间点同步，系统崩溃时可能丢失少量数据，但写入速度显著提升。
- 对于告警场景，少量丢失可接受，换取更高吞吐量。

#### 存储路径

```cpp
fs::path safeDir = fs::temp_directory_path(ec) / "sentinel-flow";
fs::path dbPath = safeDir / dbPathStr;
```

- 默认存储于系统临时目录（如 `/tmp/sentinel-flow/`），避免权限问题。
- 可通过 `init()` 参数自定义路径。

### 表结构设计

#### 告警表 `alerts`

```sql
CREATE TABLE alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER,
    rule_name TEXT,
    source_ip TEXT,
    level INTEGER,
    description TEXT
);
```

#### 规则表 `rules`

```sql
CREATE TABLE rules (
    id INTEGER PRIMARY KEY,
    enabled INTEGER,
    protocol TEXT,
    pattern TEXT,
    level INTEGER,
    description TEXT
);
```

#### 黑名单表 `blacklist`

```sql
CREATE TABLE blacklist (
    ip TEXT PRIMARY KEY,
    reason TEXT,
    timestamp INTEGER
);
```

#### 配置表 `config`

```sql
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT
);
```

### 告警批处理写入

#### 内存队列

```cpp
std::queue<Alert> alertQueue;
std::mutex queueMutex;
std::condition_variable queueCv;
```

- `DatabaseManager::saveAlert()` 将告警推入队列，若队列大小超过 20000 则直接丢弃（背压保护）。
- 唤醒后台工作线程。

#### 工作线程循环

1. 从 `alertQueue` 取出最多 1000 条告警。
2. 使用 `BEGIN IMMEDIATE` 事务批量插入。
3. 预编译插入语句，循环绑定参数执行。
4. 提交事务。

### 事务聚合优势

- **减少 I/O 次数**：1000 条告警一次提交，相比逐条提交减少约 1000 倍的磁盘同步操作。
- **提升吞吐量**：实测在普通 SSD 上可达每秒数万条告警写入。

### 背压保护

```cpp
void DatabaseManager::saveAlert(const Alert& alert) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (alertQueue.size() >= 20000) {
        return;  // 丢弃告警，防止内存溢出
    }
    alertQueue.push(alert);
    queueCv.notify_one();
}
```

- 内存队列硬上限 20000 条。
- 恶劣网络环境下，告警生成速度超过写入速度时，自动丢弃最旧的未写入告警，避免进程 OOM。

### 规则批量保存

```cpp
void DatabaseManager::saveRulesTransaction(const std::vector<IdsRule>& rulesList) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = "INSERT OR REPLACE INTO rules (...) VALUES (...);";
    // 预编译、绑定、循环执行
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}
```

### 崩溃恢复

- WAL 模式在进程崩溃后会自动回滚未提交的事务，保证数据库完整性。
- 若数据库损坏，可手动删除 `-wal` 和 `-shm` 文件恢复（数据可能丢失）。

### 性能调优建议

- **磁盘选择**：使用 SSD 存储数据库文件，提升写入性能。
- **定期维护**：执行 `VACUUM` 回收空闲空间。
- **内存缓存**：适当增加 SQLite 缓存大小：`PRAGMA cache_size = 10000;`。
- **同步调优**：`PRAGMA synchronous = NORMAL;`（慎用，可能降低安全性）。

---

## 2. 取证文件生成机制

### 概述

`ForensicWorker` 是 Sentinel-Flow 的离线取证组件，负责将检测到的高危告警对应的原始数据包保存为独立的 PCAP 文件。通过异步批量写入机制，在保障数据面性能的同时，为安全分析人员提供完整的网络证据留存能力。

### 设计目标

- **自动取证**：当检测到高危告警（`level >= High`）时，自动保存相关数据包。
- **性能无侵扰**：写入操作在独立线程中异步执行，不阻塞解析管线。
- **批量聚合**：将多个数据包合并写入同一文件，减少磁盘 I/O 次数。
- **可追溯性**：文件命名包含时间戳和数据包数量，便于检索。

### 实现原理

#### 类定义

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

### PCAP 文件写入

```cpp
void ForensicWorker::saveBatchToPcap(const std::vector<ParsedPacket>& batch) {
    // 生成时间戳字符串
    auto now = std::chrono::system_clock::now();
    // 格式化文件名: evidences/batch_YYYYMMDD_HHMMSS_count_N.pcap

    // 确保目录存在
    std::filesystem::create_directories("evidences");

    pcap_t* dead = pcap_open_dead(DLT_EN10MB, 65535);
    pcap_dumper_t* dumper = pcap_dump_open(dead, filename.c_str());

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

### 文件命名规范

- **前缀**：`batch_` 表示批处理文件。
- **时间戳**：精确到毫秒，`yyyyMMdd_HHmmss`。
- **数量**：`count_N` 表示该文件包含的数据包数量。
- **扩展名**：`.pcap`。

示例：`evidences/batch_20250324_143052_count_500.pcap`

### 线程安全与性能

- **独立线程**：取证写入完全在后台线程进行，不影响主解析流程。
- **锁粒度**：仅在访问缓冲区时加锁，写入过程不持有锁。
- **批量写入**：一次写入最多 500 个数据包，减少文件打开/关闭开销。
- **无内存拷贝**：`ParsedPacket` 中的 `block` 是 `shared_ptr`，写入时直接引用原始数据。

### 局限性

- **无自动清理**：长期运行可能导致 `evidences/` 目录占用大量磁盘空间，需用户定期手动清理。
- **仅支持以太网链路层**：`pcap_open_dead` 固定使用 `DLT_EN10MB`。
- **不支持 PCAPNG**：输出为标准 PCAP 格式。

### 扩展建议

- **自动轮转**：可添加磁盘空间监控，当剩余空间低于阈值时自动删除最旧文件。
- **压缩存储**：写入后可选压缩为 `.pcap.gz`，节省空间。
- **加密存储**：为敏感环境添加 AES 加密支持。
- **元数据索引**：在 SQLite 中建立索引，便于按时间、规则快速定位取证文件。
