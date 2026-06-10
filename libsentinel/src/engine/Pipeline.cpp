#include "sentinel/engine/Pipeline.h"
#include "sentinel/engine/DatabaseManager.h"
#include "sentinel/engine/flow/PacketParser.h"

#include <chrono>
#include <iostream>

namespace sentinel::engine {

// ---- 构造 / 析构 ----

template <size_t Capacity>
Pipeline<Capacity>::Pipeline() = default;

template <size_t Capacity>
Pipeline<Capacity>::~Pipeline() {
    stop();
}

// ---- 依赖注入 ----

template <size_t Capacity>
void Pipeline<Capacity>::set_matcher(std::shared_ptr<AhoCorasick> matcher) {
    matcher_.store(std::move(matcher), std::memory_order_release);
}

template <size_t Capacity>
void Pipeline<Capacity>::set_alert_callback(AlertCallback cb) {
    alert_cb_ = std::move(cb);
}

template <size_t Capacity>
void Pipeline<Capacity>::set_db_manager(std::shared_ptr<DatabaseManager> db) {
    db_manager_ = std::move(db);
}

template <size_t Capacity>
void Pipeline<Capacity>::bind_capture(std::shared_ptr<capture::ICapture> cap) {
    capture_ = std::move(cap);

    // 捕获回调：将 RawPacket 推入内部 SPSC 队列
    capture_->set_packet_callback([this](types::RawPacket&& raw) {
        if (!queue_.push(std::move(raw))) {
            // 队满丢弃 — 消费者处理能力不足
        }
    });
}

// ---- 生命周期 ----

template <size_t Capacity>
void Pipeline<Capacity>::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }

    auto const matcher = matcher_.load(std::memory_order_acquire);
    if (!matcher || !matcher->is_built()) {
        std::cerr << "[Pipeline] 启动失败: AC 自动机未构建" << std::endl;
        return;
    }

    running_.store(true, std::memory_order_release);
    consumer_thread_ = std::jthread(&Pipeline::consumer_loop, this);
}

template <size_t Capacity>
void Pipeline<Capacity>::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);

    // jthread 析构时自动调用 request_stop + join
    consumer_thread_.request_stop();

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

// ---- 消费者循环 ----

template <size_t Capacity>
void Pipeline<Capacity>::consumer_loop(std::stop_token stoken) {
    while (!stoken.stop_requested()) {
        auto raw_opt = queue_.pop();
        if (!raw_opt.has_value()) {
            continue; // 空队列，忙轮询
        }

        auto& raw = *raw_opt;

        // L3/L4 协议解析
        auto parsed_opt = flow::PacketParser::parse(raw);
        if (!parsed_opt.has_value()) {
            continue;
        }

        auto& parsed = *parsed_opt;

        // 原子获取最新 AC 自动机快照（release/acquire 配对保证热重载可见性）
        auto const matcher = matcher_.load(std::memory_order_acquire);

        // AC 自动机多模式匹配
        if (matcher && matcher->is_built() && parsed.payload_length > 0) {
            std::vector<int32_t> matches;
            if (matcher->match(raw.payload(), matches)) {
                // 命中规则 — 触发告警回调 + 异步持久化
                for (int32_t rule_id : matches) {
                    if (alert_cb_) {
                        alert_cb_(rule_id, parsed);
                    }
                    if (db_manager_) {
                        // 仅提取应用层载荷（跳过链路层 + IP + TCP/UDP 头）
                        size_t const total = raw.payload().size();
                        size_t const app_offset = (total > parsed.payload_length)
                            ? total - parsed.payload_length : 0;
                        auto const app_span = raw.payload().subspan(app_offset,
                            std::min(parsed.payload_length, uint32_t(256)));
                        std::string const payload_snippet(
                            reinterpret_cast<const char*>(app_span.data()),
                            app_span.size());
                        if (!payload_snippet.empty()) {
                            db_manager_->save_alert(rule_id, payload_snippet);
                        }
                    }
                }
            }
        }
    }
}

// 显式实例化（默认容量 65536）
template class Pipeline<65536>;

template class Pipeline<1024>;
template class Pipeline<4>;

} // namespace sentinel::engine
