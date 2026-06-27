#include "sentinel/common/utils/Logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <filesystem>
#include <memory>

namespace sentinel::log {

static std::shared_ptr<spdlog::logger> g_logger;

void init(const std::string& file_path) {
    if (g_logger) {
        return; // 已初始化
    }

    // 确保日志目录存在
    std::filesystem::path const p(file_path);
    if (auto parent = p.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    // 异步线程池：1 个后台线程，队列 8192 条消息
    spdlog::init_thread_pool(8192, 1);

    // 双接收器
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(file_path, 0, 0);
    file_sink->set_level(spdlog::level::trace);

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

    // 创建异步 logger
    auto logger = std::make_shared<spdlog::async_logger>(
        "sentinel_core",
        sinks.begin(),
        sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest
    );

    // 格式：[时间戳] [级别] [线程ID] [logger名称] 消息
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%n] %v");
    logger->set_level(spdlog::level::trace);

    spdlog::register_logger(logger);
    g_logger = logger;

    SENTINEL_INFO("spdlog 异步日志系统初始化完成, file={}", file_path);
}

void shutdown() {
    if (g_logger) {
        SENTINEL_INFO("日志系统正常关闭");
        g_logger->flush();
        spdlog::drop("sentinel_core");
        g_logger.reset();
    }
}

std::shared_ptr<spdlog::logger> get_logger() {
    if (!g_logger) {
        // 安全回退：未显式初始化时，创建默认同步 logger
        static auto fallback = spdlog::stderr_color_mt("sentinel_core_fallback");
        return fallback;
    }
    return g_logger;
}

} // namespace sentinel::log
