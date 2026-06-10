#include "sentinel/engine/DatabaseManager.h"

#include <sqlite3.h>

#include <cstdio>
#include <iostream>
#include <filesystem>

namespace sentinel::engine {

// ---- 构造 / 析构 ----

DatabaseManager::DatabaseManager(const std::string& db_path) {
    // 确保数据库文件所在目录存在
    std::filesystem::path const p(db_path);
    if (auto parent = p.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[DatabaseManager] 打开数据库失败: " << sqlite3_errmsg(db_) << std::endl;
        return;
    }

    // 高性能并发配置
    execute_sql("PRAGMA journal_mode=WAL;");
    execute_sql("PRAGMA synchronous=NORMAL;");
    execute_sql("PRAGMA cache_size=-8000;"); // 8MB 缓存

    create_tables();

    // 预分配缓冲区
    front_buffer_.reserve(kMaxBufferSize);
    back_buffer_.reserve(kMaxBufferSize);

    // 启动后台写入线程
    running_.store(true, std::memory_order_release);
    worker_thread_ = std::jthread(&DatabaseManager::worker_loop, this);

    std::cout << "[DatabaseManager] 已初始化, WAL 模式, 路径: " << db_path << std::endl;
}

DatabaseManager::~DatabaseManager() {
    shutdown();
}

// ---- 表创建 ----

void DatabaseManager::create_tables() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS alerts (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            rule_id    INTEGER NOT NULL,
            payload    TEXT,
            timestamp  DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )SQL";
    execute_sql(sql);
}

// ---- 异步保存 ----

void DatabaseManager::save_alert(int32_t rule_id, std::string_view payload) {
    std::lock_guard lock(buffer_mutex_);

    if (front_buffer_.size() >= kMaxBufferSize) {
        // 背压保护：丢弃最旧告警
        front_buffer_.erase(front_buffer_.begin(),
                            front_buffer_.begin() + front_buffer_.size() / 4);
    }

    front_buffer_.push_back(Alert{rule_id, std::string(payload)});

    // 达到批量阈值时唤醒后台线程
    if (front_buffer_.size() >= kBatchSize) {
        buffer_cv_.notify_one();
    }
}

// ---- 同步冲刷 ----

void DatabaseManager::flush() {
    // 唤醒后台线程立即处理
    buffer_cv_.notify_one();

    // 等待双缓冲均清空（worker_loop 将 front_ → back_ → SQLite → clear）
    for (int retry = 0; retry < 100; ++retry) {
        {
            std::lock_guard lock(buffer_mutex_);
            if (front_buffer_.empty() && back_buffer_.empty()) {
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 超时：最后一次尝试唤醒
    buffer_cv_.notify_one();
}

// ---- 查询 ----

std::vector<DatabaseManager::Alert> DatabaseManager::recent_alerts(int limit) const {
    std::vector<Alert> results;
    if (!db_) return results;

    auto sql = "SELECT rule_id, payload FROM alerts ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Alert a;
        a.rule_id = sqlite3_column_int(stmt, 0);
        auto const* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (text) a.payload = text;
        results.push_back(std::move(a));
    }

    sqlite3_finalize(stmt);
    return results;
}

// ---- 后台写入线程 ----

void DatabaseManager::worker_loop(std::stop_token stoken) {
    while (!stoken.stop_requested()) {
        // 等待缓冲区积累或超时
        {
            std::unique_lock lock(buffer_mutex_);
            buffer_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return front_buffer_.size() >= kBatchSize;
            });

            if (front_buffer_.empty()) {
                continue;
            }

            // 交换缓冲区：front ↔ back
            back_buffer_.swap(front_buffer_);
        }

        // 锁外批量写入 SQLite
        if (!back_buffer_.empty() && db_) {
            execute_sql("BEGIN IMMEDIATE;");

            const char* insert_sql =
                "INSERT INTO alerts (rule_id, payload) VALUES (?, ?);";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (auto const& alert : back_buffer_) {
                    sqlite3_bind_int(stmt, 1, alert.rule_id);
                    sqlite3_bind_text(stmt, 2, alert.payload.c_str(),
                                      static_cast<int>(alert.payload.size()),
                                      SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                    sqlite3_clear_bindings(stmt);
                }
                sqlite3_finalize(stmt);
            }

            execute_sql("COMMIT;");

            total_alerts_.fetch_add(static_cast<int64_t>(back_buffer_.size()),
                                    std::memory_order_relaxed);
            back_buffer_.clear();
        }
    }

    // 最终冲刷：写入所有剩余告警
    if (!front_buffer_.empty() && db_) {
        back_buffer_.swap(front_buffer_);
        execute_sql("BEGIN IMMEDIATE;");
        for (auto const& alert : back_buffer_) {
            auto sql = "INSERT INTO alerts (rule_id, payload) VALUES ("
                       + std::to_string(alert.rule_id) + ", '"
                       + alert.payload + "');";
            execute_sql(sql.c_str());
        }
        execute_sql("COMMIT;");
        total_alerts_.fetch_add(static_cast<int64_t>(back_buffer_.size()),
                                std::memory_order_relaxed);
        back_buffer_.clear();
    }
}

// ---- 停止 ----

void DatabaseManager::shutdown() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);
    buffer_cv_.notify_all();
    worker_thread_.request_stop();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // WAL checkpoint 确保数据完整落盘
    if (db_) {
        execute_sql("PRAGMA wal_checkpoint(TRUNCATE);");
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ---- 内部辅助 ----

void DatabaseManager::execute_sql(const char* sql) {
    if (!db_) return;
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[DatabaseManager] SQL 错误: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

} // namespace sentinel::engine
