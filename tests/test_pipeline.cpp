#include <gtest/gtest.h>
#include "sentinel/engine/Pipeline.h"
#include "sentinel/engine/match/AhoCorasick.h"
#include "sentinel/capture/ICapture.h"
#include "sentinel/types/PacketTypes.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <thread>

using namespace sentinel;
using namespace sentinel::engine;
using namespace sentinel::capture;
using namespace sentinel::types;

// ---- 辅助：模拟捕获驱动 ----
// 实现 ICapture 接口，手动注入预构造的 RawPacket 用于测试。
class MockCapture final : public ICapture {
public:
    void set_packet_callback(PacketCallback cb) override {
        callback_ = std::move(cb);
    }

    bool start(const std::string&) override {
        started_ = true;
        return true;
    }

    void stop() override {
        started_ = false;
    }

    bool set_filter(const std::string&) override {
        return true;
    }

    std::vector<std::string> list_devices() override {
        return {};
    }

    // 直接注入报文到回调（模拟网卡收到数据）。
    void inject(RawPacket&& raw) {
        if (callback_) {
            callback_(std::move(raw));
        }
    }

    bool started() const { return started_; }

private:
    PacketCallback callback_;
    bool started_{false};
};

// ---- 辅助：构造包含特定载荷的 TCP 报文 ----
static RawPacket make_tcp_payload(const char* text) {
    size_t const text_len = std::strlen(text);
    constexpr size_t kEthHdr = 14;
    constexpr size_t kIpHdr  = 20;
    constexpr size_t kTcpHdr = 20;
    size_t const total_size = kEthHdr + kIpHdr + kTcpHdr + text_len;

    auto block = std::make_shared<MemoryBlock>();
    block->size = static_cast<uint32_t>(total_size);
    std::memset(block->data, 0, total_size);

    uint8_t* p = block->data + kEthHdr;

    // IP 头部
    p[0] = 0x45;
    p[2] = static_cast<uint8_t>((total_size - kEthHdr) >> 8);
    p[3] = static_cast<uint8_t>((total_size - kEthHdr) & 0xFF);
    p[8] = 64;
    p[9] = IPPROTO_TCP;
    p[12] = 192; p[13] = 168; p[14] = 1; p[15] = 1;
    p[16] = 10;  p[17] = 0;  p[18] = 0; p[19] = 2;

    p += kIpHdr;

    // TCP 头部（端口 12345 → 80，SYN+ACK）
    p[0] = static_cast<uint8_t>(12345 >> 8);
    p[1] = static_cast<uint8_t>(12345 & 0xFF);
    p[2] = static_cast<uint8_t>(80 >> 8);
    p[3] = static_cast<uint8_t>(80 & 0xFF);
    p[12] = 0x50;
    p[13] = 0x12;

    p += kTcpHdr;

    std::memcpy(p, text, text_len);

    RawPacket raw;
    raw.kernel_timestamp_ns = 1'000'000'000;
    raw.block = block;
    raw.link_layer_offset = kEthHdr;

    return raw;
}

// ---- 管线启动/停止 ----
TEST(PipelineTest, StartStop) {
    auto matcher = std::make_shared<AhoCorasick>();
    matcher->insert("test", 1);
    matcher->build();

    Pipeline<1024> pipeline;
    pipeline.set_matcher(matcher);

    EXPECT_FALSE(pipeline.is_running());
    pipeline.start();
    EXPECT_TRUE(pipeline.is_running());
    pipeline.stop();
    EXPECT_FALSE(pipeline.is_running());
}

// ---- 未 build 自动机拒绝启动 ----
TEST(PipelineTest, RefuseStartWithoutBuiltMatcher) {
    auto matcher = std::make_shared<AhoCorasick>();
    // 未调用 build()

    Pipeline<1024> pipeline;
    pipeline.set_matcher(matcher);
    pipeline.start();
    EXPECT_FALSE(pipeline.is_running());
}

// ---- 告警回调触发 ----
TEST(PipelineTest, AlertCallbackFiresOnMatch) {
    auto matcher = std::make_shared<AhoCorasick>();
    matcher->insert("malware_payload", 9999);
    matcher->build();

    std::atomic<int> alert_count{0};
    std::atomic<int32_t> last_rule_id{0};

    Pipeline<1024> pipeline;
    pipeline.set_matcher(matcher);
    pipeline.set_alert_callback([&](int32_t rule_id, const ParsedPacket&) {
        alert_count.fetch_add(1, std::memory_order_relaxed);
        last_rule_id.store(rule_id, std::memory_order_relaxed);
    });

    auto mock = std::make_shared<MockCapture>();
    pipeline.bind_capture(mock);
    pipeline.start();
    ASSERT_TRUE(pipeline.is_running());

    // 模拟捕获驱动注入包含恶意载荷的报文
    auto raw = make_tcp_payload("GET / HTTP/1.1\r\nmalware_payload\r\n");
    mock->inject(std::move(raw));

    // 等待消费者处理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(alert_count.load(), 1);
    EXPECT_EQ(last_rule_id.load(), 9999);

    pipeline.stop();
}

// ---- 无匹配时回调不触发 ----
TEST(PipelineTest, NoAlertOnBenignTraffic) {
    auto matcher = std::make_shared<AhoCorasick>();
    matcher->insert("malware_payload", 9999);
    matcher->build();

    std::atomic<int> alert_count{0};

    Pipeline<1024> pipeline;
    pipeline.set_matcher(matcher);
    pipeline.set_alert_callback([&](int32_t, const ParsedPacket&) {
        alert_count.fetch_add(1, std::memory_order_relaxed);
    });

    auto mock = std::make_shared<MockCapture>();
    pipeline.bind_capture(mock);
    pipeline.start();

    auto raw = make_tcp_payload("GET / HTTP/1.1\r\nbenign_data\r\n");
    mock->inject(std::move(raw));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(alert_count.load(), 0);

    pipeline.stop();
}

// ---- 多条规则匹配 ----
TEST(PipelineTest, MultipleRuleMatches) {
    auto matcher = std::make_shared<AhoCorasick>();
    matcher->insert("attack_one", 100);
    matcher->insert("attack_two", 200);
    matcher->build();

    std::atomic<int> alert_count{0};
    std::vector<int32_t> matched_ids;
    std::mutex ids_mutex;

    Pipeline<1024> pipeline;
    pipeline.set_matcher(matcher);
    pipeline.set_alert_callback([&](int32_t rule_id, const ParsedPacket&) {
        alert_count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(ids_mutex);
        matched_ids.push_back(rule_id);
    });

    auto mock = std::make_shared<MockCapture>();
    pipeline.bind_capture(mock);
    pipeline.start();

    // 载荷同时包含两个攻击模式
    auto raw = make_tcp_payload("attack_one_and_attack_two_in_same_packet");
    mock->inject(std::move(raw));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(alert_count.load(), 2);

    pipeline.stop();
}

// ---- 队列满时丢弃报文 ----
TEST(PipelineTest, DropOnQueueFull) {
    auto matcher = std::make_shared<AhoCorasick>();
    matcher->insert("test", 1);
    matcher->build();

    Pipeline<4> pipeline; // 极小容量，容量 4 最多存 3 个
    pipeline.set_matcher(matcher);

    auto mock = std::make_shared<MockCapture>();
    pipeline.bind_capture(mock);

    // 注入大量报文，部分将被丢弃（不崩溃）
    pipeline.start();
    for (int i = 0; i < 100; ++i) {
        auto raw = make_tcp_payload("test_data");
        mock->inject(std::move(raw));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pipeline.stop();
    // 通过不崩溃来验证正确性
    SUCCEED();
}
