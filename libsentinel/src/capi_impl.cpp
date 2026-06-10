#include "sentinel/capi.h"

#include "sentinel/capture/PcapCapture.h"
#include "sentinel/engine/DatabaseManager.h"
#include "sentinel/engine/Pipeline.h"
#include "sentinel/engine/match/AhoCorasick.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

// 内部上下文：封装 Pipeline + AC 自动机 + 捕获驱动 + 数据库 + 告警回调。
struct EngineContext {
    std::shared_ptr<sentinel::engine::AhoCorasick> matcher;
    std::shared_ptr<sentinel::capture::PcapCapture> capture;
    std::shared_ptr<sentinel::engine::DatabaseManager> database;
    sentinel::engine::Pipeline<65536> pipeline;

    sentinel_alert_callback_t alert_callback = nullptr;
    void* alert_user_data = nullptr;

    // 待构建的规则列表
    std::vector<std::pair<std::string, int>> pending_rules;
};

extern "C" {

// ---- 引擎生命周期 ----

sentinel_engine_t sentinel_engine_create(void) {
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
                if (ctx->alert_callback) {
                    // 提取载荷快照作为 C 字符串（栈临时，回调内有效）
                    std::string const snippet = parsed.protocol + " match rule " + std::to_string(rule_id);
                    ctx->alert_callback(rule_id, snippet.c_str(), ctx->alert_user_data);
                }
            });

        return static_cast<sentinel_engine_t>(ctx);
    } catch (const std::exception& e) {
        std::cerr << "[capi] 引擎创建失败: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "[capi] 引擎创建失败: 未知异常" << std::endl;
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
    } catch (...) {
        // 析构不应抛异常，防御性捕获
    }
}

int sentinel_engine_start(sentinel_engine_t engine, const char* device) {
    if (!engine || !device) {
        return -1;
    }
    (void)engine; // 参数保留用于后续扩展（如多引擎管理）

    try {
        auto* ctx = static_cast<EngineContext*>(engine);

        // 确保自动机已构建
        if (!ctx->matcher->is_built()) {
            std::cerr << "[capi] 启动失败: AC 自动机未构建" << std::endl;
            return -2;
        }

        // 启动捕获驱动
        std::string const device_str(device);
        if (!ctx->capture->start(device_str)) {
            std::cerr << "[capi] 启动失败: 无法打开设备 " << device << std::endl;
            return -3;
        }

        // 启动管线
        ctx->pipeline.start();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[capi] 启动异常: " << e.what() << std::endl;
        return -4;
    } catch (...) {
        std::cerr << "[capi] 启动异常: 未知错误" << std::endl;
        return -4;
    }
}

void sentinel_engine_stop(sentinel_engine_t engine) {
    if (!engine) {
        return;
    }
    try {
        auto* ctx = static_cast<EngineContext*>(engine);
        ctx->pipeline.stop();
        ctx->capture->stop();
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
        std::cerr << "[capi] 添加规则失败: " << e.what() << std::endl;
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
        std::cerr << "[capi] 构建自动机失败: " << e.what() << std::endl;
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

} // extern "C"
