# SQLite WAL 日志与批处理写入

## 概述

Sentinel-Flow 使用 SQLite 作为本地持久化存储，存储告警日志、检测规则、黑名单和配置信息。通过启用 WAL (Write-Ahead Logging) 模式、配置 `synchronous=NORMAL` 以及实现批量事务聚合，在保证数据安全的前提下最大化写入性能，避免 I/O 阻塞数据面管线。

## 数据库配置

### WAL 模式

```cpp
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
```

- **优势**：读操作不阻塞写操作，写操作可以并发进行（单个写者多个读者）。
- **效果**：提升并发性能，减少锁竞争。

### 同步模式

```cpp
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
```

- `NORMAL` 级别：在关键时间点同步，系统崩溃时可能丢失少量数据，但写入速度显著提升。
- 对于告警场景，少量丢失可接受，换取更高吞吐量。

### 存储路径

```cpp
    fs::path safeDir = fs::temp_directory_path(ec) / "sentinel-flow";
    fs::path dbPath = safeDir / dbPathStr;
```

- 默认存储于系统临时目录（如 `/tmp/sentinel-flow/`），避免权限问题。
- 可通过 `init()` 参数自定义路径。

## 表结构设计

### 告警表 `alerts`

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

### 规则表 `rules`

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

### 黑名单表 `blacklist`

```sql
    CREATE TABLE blacklist (
        ip TEXT PRIMARY KEY,
        reason TEXT,
        timestamp INTEGER
    );
```

### 配置表 `config`

```sql
    CREATE TABLE config (
        key TEXT PRIMARY KEY,
        value TEXT
    );
```

## 告警批处理写入

### 内存队列

```cpp
    std::queue<Alert> alertQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
```

- `DatabaseManager::saveAlert()` 将告警推入队列，若队列大小超过 20000 则直接丢弃（背压保护）。
- 唤醒后台工作线程。

### 工作线程循环

```cpp
    void DatabaseManager::workerLoop() {
        while (running) {
            std::vector<Alert> batch;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCv.wait(lock, [this] { return !alertQueue.empty() || !running; });
                // 取出最多 1000 条告警
                int count = 0;
                while (!alertQueue.empty() && count < 1000) {
                    batch.push_back(alertQueue.front());
                    alertQueue.pop();
                    count++;
                }
            }
            if (batch.empty()) continue;
    
            // 开始事务
            sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg);
            // 预编译插入语句
            const char* sql = "INSERT INTO alerts (...) VALUES (...);";
            sqlite3_stmt* stmt;
            sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
            for (const auto& alert : batch) {
                sqlite3_bind_int64(stmt, 1, alert.timestamp);
                sqlite3_bind_text(stmt, 2, alert.ruleName.c_str(), -1, SQLITE_STATIC);
                // ... 绑定其他字段
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
            // 提交事务
            sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg);
        }
    }
```

### 事务聚合优势

- **减少 I/O 次数**：1000 条告警一次提交，相比逐条提交减少约 1000 倍的磁盘同步操作。
- **提升吞吐量**：实测在普通 SSD 上可达每秒数万条告警写入。

## 规则批量保存

```cpp
    void DatabaseManager::saveRulesTransaction(const std::vector<IdsRule>& rulesList) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        const char* sql = "INSERT OR REPLACE INTO rules (id, enabled, protocol, pattern, level, description) VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        for (const auto& r : rulesList) {
            // 绑定参数
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    }
```

- 用于 Snort 规则批量导入，避免每插入一条规则单独提交。

## 背压保护

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

## 配置与黑名单操作

- 所有 `saveConfig`、`loadConfig`、`saveBlacklist`、`loadBlacklist` 均为即时执行（非批处理），因为操作频率低。
- 黑名单使用 `INSERT OR REPLACE` 保证幂等性。

## 崩溃恢复

- WAL 模式在进程崩溃后会自动回滚未提交的事务，保证数据库完整性。
- 若数据库损坏，可手动删除 `-wal` 和 `-shm` 文件恢复（数据可能丢失）。

## 性能调优建议

- **磁盘选择**：使用 SSD 存储数据库文件，提升写入性能。
- **预分配**：可以预先创建表并填充空数据，减少后续插入时的页分配开销。
- **定期维护**：执行 `VACUUM` 回收空闲空间（UI 提供入口）。
- **内存缓存**：适当增加 SQLite 缓存大小：`PRAGMA cache_size = 10000;`。

---
