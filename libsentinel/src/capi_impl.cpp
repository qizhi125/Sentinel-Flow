#include "sentinel/capi.h"

#include "sentinel/capture/PcapCapture.h"
#include "sentinel/common/utils/Logger.h"
#include "sentinel/engine/DatabaseManager.h"
#include "sentinel/engine/Pipeline.h"
#include "sentinel/engine/match/AhoCorasick.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 内部上下文：封装 Pipeline + AC 自动机 + 捕获驱动 + 数据库 + 告警 + 遥测回调。
struct EngineContext {
    std::shared_ptr<sentinel::engine::AhoCorasick> matcher;
    std::shared_ptr<sentinel::capture::PcapCapture> capture;
    std::shared_ptr<sentinel::engine::DatabaseManager> database;
    sentinel::engine::Pipeline<65536> pipeline;

    sentinel_alert_callback_t alert_callback = nullptr;
    void* alert_user_data = nullptr;

    sentinel_stats_callback_t stats_callback = nullptr;
    void* stats_user_data = nullptr;

    // 待构建的规则列表
    std::vector<std::pair<std::string, int>> pending_rules;

    // 遥测统计线程（约 1 Hz 周期推送引擎指标）
    std::jthread stats_thread;
};

extern "C" {

// ---- 引擎生命周期 ----

sentinel_engine_t sentinel_engine_create(void) {
    // 初始化异步日志系统（双接收器：stderr + 每日滚动文件）
    sentinel::log::init();

    try {
        auto* ctx = new EngineContext();

        ctx->matcher = std::make_shared<sentinel::engine::AhoCorasick>();
        ctx->capture = std::make_shared<sentinel::capture::PcapCapture>();
        ctx->database = std::make_shared<sentinel::engine::DatabaseManager>();

        // 装配管线：捕获 → 解析 → 匹配 → 告警 + 持久化
        ctx->pipeline.set_matcher(ctx->matcher);
        ctx->pipeline.set_db_manager(ctx->database);
        ctx->pipeline.bind_capture(ctx->capture);

        ctx->pipeline.set_alert_callback(
            [ctx](int32_t rule_id, sentinel::types::ParsedPacket const& parsed) {
                if (!ctx->alert_callback) {
                    return;
                }

                // 栈分配载荷快照缓冲区（提取应用层数据首部 127 字节）
                char snippet_buf[128];
                snippet_buf[0] = '\0';

                if (parsed.block && parsed.payload_length > 0) {
                    size_t const total = parsed.block->size;
                    size_t const app_offset = (total > parsed.payload_length)
                        ? total - parsed.payload_length : 0;
                    size_t const copy_len = (app_offset < total)
                        ? std::min<size_t>(total - app_offset,
                              std::min<size_t>(parsed.payload_length, 127))
                        : 0;
                    if (copy_len > 0) {
                        std::memcpy(snippet_buf, parsed.block->data + app_offset, copy_len);
                        snippet_buf[copy_len] = '\0';
                    }
                }

                // 栈分配五元组事件结构体（零动态分配）
                sentinel_alert_event_t event{};
                event.rule_id = static_cast<int>(rule_id);
                event.src_ip = htonl(parsed.src_ip);
                event.dst_ip = htonl(parsed.dst_ip);
                event.src_port = htons(parsed.src_port);
                event.dst_port = htons(parsed.dst_port);
                event.protocol = parsed.ip_protocol;    // IANA 协议号（IPPROTO_TCP=6, 等）
                event.timestamp_ns = parsed.timestamp_ns;
                event.payload_snippet = snippet_buf;

                ctx->alert_callback(&event, ctx->alert_user_data);
            });

        SENTINEL_INFO("引擎创建成功");
        return static_cast<sentinel_engine_t>(ctx);
    } catch (const std::exception& e) {
        SENTINEL_ERROR("引擎创建失败: {}", e.what());
        return nullptr;
    } catch (...) {
        SENTINEL_ERROR("引擎创建失败: 未知异常");
        return nullptr;
    }
}

void sentinel_engine_destroy(sentinel_engine_t engine) {
    if (!engine) {
        return;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);
        delete ctx;
        SENTINEL_INFO("引擎已销毁");
    } catch (...) {
        // 析构不应抛异常，防御性捕获
    }

    // 冲刷日志并关闭
    sentinel::log::shutdown();
}

int sentinel_engine_start(sentinel_engine_t engine, const char* device) {
    if (!engine || !device) {
        return -1;
    }
    (void)engine;

    try {
        auto* ctx = static_cast<EngineContext*>(engine);

        // 确保自动机已构建
        if (!ctx->matcher->is_built()) {
            SENTINEL_ERROR("启动失败: AC 自动机未构建");
            return -2;
        }

        // 启动捕获驱动
        std::string const device_str(device);
        if (!ctx->capture->start(device_str)) {
            SENTINEL_ERROR("启动失败: 无法打开设备 {}", device);
            return -3;
        }

        // 启动管线
        ctx->pipeline.start();

        // 启动遥测统计线程（约 1 Hz）
        ctx->stats_thread = std::jthread(
            [](std::stop_token stoken, EngineContext* ctx) {
                while (!stoken.stop_requested()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                    if (!ctx->stats_callback) continue;

                    sentinel_engine_stats_t stats{};
                    stats.packets_received = ctx->capture->get_packets_received();
                    stats.packets_dropped  = ctx->capture->get_packets_dropped();
                    stats.queue_depth      = static_cast<uint32_t>(ctx->pipeline.queue_size());
                    stats.db_buffer_usage  = static_cast<uint32_t>(ctx->database->get_front_buffer_size());
                    stats.has_fatal_error  = ctx->pipeline.has_error();

                    ctx->stats_callback(&stats, ctx->stats_user_data);
                }
            },
            ctx);

        SENTINEL_INFO("引擎启动成功, device={}", device_str);
        return 0;
    } catch (const std::exception& e) {
        SENTINEL_ERROR("启动异常: {}", e.what());
        return -4;
    } catch (...) {
        SENTINEL_ERROR("启动异常: 未知错误");
        return -4;
    }
}

void sentinel_engine_stop(sentinel_engine_t engine) {
    if (!engine) {
        return;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);

        ctx->stats_thread.request_stop();

        ctx->pipeline.stop();
        ctx->capture->stop();

        if (ctx->stats_thread.joinable()) {
            ctx->stats_thread.join();
        }
    } catch (...) {
        // 静默处理停止异常
    }
}

// ---- 规则管理 ----

int sentinel_engine_add_rule(sentinel_engine_t engine, const char* pattern, int rule_id) {
    if (!engine || !pattern) {
        return -1;
    }
    (void)engine; // 参数保留

    try {
        auto* ctx = static_cast<EngineContext*>(engine);
        ctx->pending_rules.emplace_back(pattern, rule_id);
        return 0;
    } catch (const std::exception& e) {
        SENTINEL_ERROR("添加规则失败: {}", e.what());
        return -2;
    } catch (...) {
        return -2;
    }
}

void sentinel_engine_clear_rules(sentinel_engine_t engine) {
    if (!engine) {
        return;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);
        ctx->pending_rules.clear();
    } catch (...) {}
}

int sentinel_engine_build_matcher(sentinel_engine_t engine) {
    if (!engine) {
        return -1;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);

        // 创建一个新的自动机并在锁外构建（避免阻塞数据面）
        auto new_matcher = std::make_shared<sentinel::engine::AhoCorasick>();
        for (auto const& [pattern, rule_id] : ctx->pending_rules) {
            new_matcher->insert(pattern, rule_id);
        }
        new_matcher->build();

        // 原子替换 matcher（Pipeline 持有 shared_ptr，下次 pop 后生效）
        ctx->matcher = std::move(new_matcher);
        ctx->pipeline.set_matcher(ctx->matcher);

        return 0;
    } catch (const std::exception& e) {
        SENTINEL_ERROR("构建自动机失败: {}", e.what());
        return -2;
    } catch (...) {
        return -2;
    }
}

int sentinel_engine_rule_count(sentinel_engine_t engine) {
    if (!engine) {
        return 0;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);
        return static_cast<int>(ctx->pending_rules.size());
    } catch (...) {
        return 0;
    }
}

// ---- 告警回调 ----

void sentinel_engine_set_alert_callback(sentinel_engine_t engine,
                                        sentinel_alert_callback_t callback,
                                        void* user_data) {
    if (!engine) {
        return;
    }
    auto* ctx = static_cast<EngineContext*>(engine);
    ctx->alert_callback = callback;
    ctx->alert_user_data = user_data;
}

// ---- 遥测统计回调 ----

void sentinel_engine_set_stats_callback(sentinel_engine_t engine,
                                        sentinel_stats_callback_t callback,
                                        void* user_data) {
    if (!engine) {
        return;
    }
    auto* ctx = static_cast<EngineContext*>(engine);
    ctx->stats_callback = callback;
    ctx->stats_user_data = user_data;
}

} // extern "C"
