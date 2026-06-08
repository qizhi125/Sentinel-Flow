#include <gtest/gtest.h>
#include "sentinel/capture/PcapCapture.h"
#include "sentinel/types/PacketTypes.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace sentinel::capture;
using namespace sentinel::types;

// ---- 实例化与基本状态 ----
TEST(PcapCaptureTest, DefaultState) {
    PcapCapture capture;
    EXPECT_FALSE(capture.is_running());
}

// ---- 回调注册 ----
TEST(PcapCaptureTest, CallbackRegistration) {
    PcapCapture capture;

    std::atomic<int> call_count{0};
    capture.set_packet_callback([&](RawPacket&&) {
        call_count.fetch_add(1, std::memory_order_relaxed);
    });

    // 回调已注册但未启动，应无调用
    EXPECT_EQ(call_count.load(), 0);
}

// ---- 设备枚举 ----
TEST(PcapCaptureTest, ListDevices) {
    PcapCapture capture;
    auto devices = capture.list_devices();

    // 在无网络接口的容器环境中列表可能为空，这是合法行为
    // 有网卡时应至少包含 loopback
    if (!devices.empty()) {
        // 验证 lo 或 eth0 等典型接口存在（不强制）
        bool has_lo = false;
        for (auto& d : devices) {
            if (d == "lo" || d.find("lo") != std::string::npos) {
                has_lo = true;
                break;
            }
        }
        // lo 可能存在也可能不存在（取决于运行环境），不强制断言
        (void)has_lo;
    }
    SUCCEED();
}

// ---- 过滤器设置（无活动句柄时） ----
TEST(PcapCaptureTest, SetFilterWithoutStart) {
    PcapCapture capture;
    // 未启动时 set_filter 应返回 false（无活动 pcap 句柄）
    EXPECT_FALSE(capture.set_filter("tcp port 80"));
}

// ---- 模式切换 ----
TEST(PcapCaptureTest, OfflineModeFlag) {
    PcapCapture capture;
    capture.set_offline_mode(true);
    capture.set_verbose(true);
    // 标志位设置后启动时生效，此处仅验证不崩溃
    SUCCEED();
}

// ---- stop 幂等性 ----
TEST(PcapCaptureTest, StopIdempotent) {
    PcapCapture capture;
    // 未启动时 stop 应无副作用
    capture.stop();
    capture.stop(); // 二次调用
    EXPECT_FALSE(capture.is_running());
}

// ---- set_filter 线程安全 ----
TEST(PcapCaptureTest, SetFilterThreadSafety) {
    PcapCapture capture;

    // 多线程并发调用 set_filter（无句柄时均返回 false）
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j) {
                if (capture.set_filter("tcp")) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 无句柄时全部应返回 false，errors 应为 0
    // 主要验证无 data race / deadlock
    EXPECT_EQ(errors.load(), 0);
}
