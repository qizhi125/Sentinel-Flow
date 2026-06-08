#include <gtest/gtest.h>
#include "sentinel/common/SPSCQueue.h"

#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

using sentinel::SPSCQueue;

// 基本单线程 push/pop
TEST(SPSCQueueTest, SingleThreadPushPop) {
    SPSCQueue<int, 16> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);

    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(q.push(i));
    }
    EXPECT_FALSE(q.empty());

    for (int i = 0; i < 10; ++i) {
        auto v = q.pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
    EXPECT_TRUE(q.empty());
}

// 队满拒绝
TEST(SPSCQueueTest, FullQueueRejection) {
    SPSCQueue<int, 4> q;
    // Capacity=4，最多存放 3 个元素
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4));  // 队满
    EXPECT_EQ(q.size(), 3u);
}

// Pop 空队列返回 nullopt
TEST(SPSCQueueTest, EmptyQueueReturnsNullopt) {
    SPSCQueue<int, 8> q;
    EXPECT_FALSE(q.pop().has_value());
}

// 并发生产者/消费者数据完整性
TEST(SPSCQueueTest, ConcurrentDataIntegrity) {
    constexpr int kItems = 1'000'000;
    SPSCQueue<uint64_t, 65536> q;

    std::atomic<bool> producerDone{false};
    uint64_t producerSum = 0;
    uint64_t consumerSum = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            uint64_t val = static_cast<uint64_t>(i);
            while (!q.push(val)) {}
            producerSum += val;
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int consumed = 0;
        while (consumed < kItems) {
            auto item = q.pop();
            if (item.has_value()) {
                consumerSum += *item;
                ++consumed;
            } else if (producerDone.load(std::memory_order_acquire)) {
                // 生产者已完成但还有元素未取完，继续轮询
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(producerSum, consumerSum);
}

// 阻塞等待 popWait
TEST(SPSCQueueTest, BlockingPopWait) {
    SPSCQueue<int, 8> q;

    std::thread producer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        q.push(42);
    });

    int val = q.popWait();
    EXPECT_EQ(val, 42);

    producer.join();
}

// Move-only 类型支持
TEST(SPSCQueueTest, MoveOnlyType) {
    auto u1 = std::make_unique<int>(10);
    auto* ptr = u1.get();

    SPSCQueue<std::unique_ptr<int>, 8> q;
    EXPECT_TRUE(q.push(std::move(u1)));
    EXPECT_EQ(u1, nullptr);

    auto u2 = q.pop();
    ASSERT_TRUE(u2.has_value());
    EXPECT_EQ(u2->get(), ptr);
    EXPECT_EQ(**u2, 10);
}
