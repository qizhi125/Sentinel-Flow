#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 前置声明
struct sqlite3;

namespace sentinel::engine {

// 异步告警持久化管理器。
//
// 双缓冲 + 后台线程批量写入 SQLite（WAL 模式）。
// Pipeline 线程调用 save_alert() 仅加锁推送，不执行 I/O。
class DatabaseManager {
public:
    // 告警记录。
    struct Alert {
        int32_t rule_id;
        std::string payload;
    };

    explicit DatabaseManager(const std::string& db_path = "sentinel_alerts.db");
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    // 异步保存告警（非阻塞，仅推入缓冲区）。
    // 若缓冲区超过上限则丢弃最旧记录，防止内存溢出。
    void save_alert(int32_t rule_id, std::string_view payload);

    // 查询最近 N 条告警（仅用于 Dashboard/API，非热路径）。
    [[nodiscard]] std::vector<Alert> recent_alerts(int limit = 100) const;

    // 同步冲刷：立即将当前缓冲区中的所有告警写入 SQLite。
    // 阻塞直到写入完成。用于测试和正常关闭前的确定性落盘。
    void flush();

    // 告警总数。
    [[nodiscard]] int64_t total_alerts() const noexcept {
        return total_alerts_.load(std::memory_order_relaxed);
    }

    // 前端缓冲区当前条目数（近似值，用于遥测）。
    [[nodiscard]] size_t get_front_buffer_size() const {
        std::lock_guard lock(buffer_mutex_);
        return front_buffer_.size();
    }

    // 停止后台线程并等待所有待写入告警落盘。
    void shutdown();

private:
    void worker_loop(std::stop_token stoken);

    // SQLite 辅助
    void execute_sql(const char* sql);
    void create_tables();

    sqlite3* db_ = nullptr;

    // 双缓冲：front_ 由 Pipeline 线程写入，back_ 由 DB 线程批量持久化。
    std::vector<Alert> front_buffer_;
    std::vector<Alert> back_buffer_;
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;

    static constexpr size_t kMaxBufferSize = 10000;
    static constexpr size_t kBatchSize = 500;

    std::jthread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> total_alerts_{0};
};

} // namespace sentinel::engine
