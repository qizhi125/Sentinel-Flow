#pragma once

#include "sentinel/capture/ICapture.h"
#include "sentinel/common/SPSCQueue.h"
#include "sentinel/engine/match/AhoCorasick.h"
#include "sentinel/types/PacketTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace sentinel::engine {

// 数据面管线编排器。
//
// 持有 SPSC 队列、捕获驱动引用、AC 自动机和消费者线程。
// 捕获回调 → push 队列 → consumer_loop: pop → parse → match → alert callback。
// matcher_ 使用 atomic<shared_ptr> 实现无锁热交换。
//
// @tparam Capacity  SPSC 环形队列容量（必须是 2 的幂，默认 8192）。
template <size_t Capacity = 8192>
class Pipeline {
public:
    // 告警回调签名：rule_id + 载荷快照。
    using AlertCallback = std::function<void(int32_t rule_id, types::ParsedPacket const&)>;

    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    // ---- 依赖注入 ----

    // 设置 AC 自动机（store(release)，消费者线程通过 load(acquire) 可见）。
    void set_matcher(std::shared_ptr<AhoCorasick> matcher);

    // 设置告警回调。
    void set_alert_callback(AlertCallback cb);

    // 绑定捕获驱动：注册回调将 RawPacket 推入内部队列。
    void bind_capture(std::shared_ptr<capture::ICapture> cap);

    // 设置数据库管理器（异步告警持久化）。
    void set_db_manager(std::shared_ptr<class DatabaseManager> db);

    // ---- 生命周期 ----

    // 启动消费者线程。幂等。
    void start();

    // 通知消费者线程停止，阻塞等待退出。幂等。
    void stop();

    // 是否正在运行。
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // SPSC 队列当前深度（近似值，用于遥测）。
    [[nodiscard]] size_t queue_size() const noexcept { return queue_.size(); }

    // 消费者线程是否已因异常退出。Go 侧通过 stats 回调轮询此标志。
    [[nodiscard]] bool has_error() const noexcept {
        return error_state_.load(std::memory_order_relaxed);
    }

private:
    void consumer_loop(std::stop_token stoken);

    SPSCQueue<types::RawPacket, Capacity> queue_;
    std::shared_ptr<capture::ICapture> capture_;
    std::atomic<std::shared_ptr<AhoCorasick>> matcher_{std::make_shared<AhoCorasick>()};
    AlertCallback alert_cb_;
    std::shared_ptr<class DatabaseManager> db_manager_;

    std::jthread consumer_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> error_state_{false}; // 消费者线程异常退出标志（stats 回调轮询）
};

// 显式实例化声明（实现在 .cpp 中）
extern template class Pipeline<8192>;

} // namespace sentinel::engine
