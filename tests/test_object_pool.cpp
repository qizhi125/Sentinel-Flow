#include <gtest/gtest.h>
#include "sentinel/common/memory/ObjectPool.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

using namespace sentinel::common;

// ---- 基本获取/释放 ----
TEST(ObjectPoolTest, AcquireRelease) {
    ObjectPool<int> pool(10);

    auto p1 = pool.acquire();
    *p1 = 42;
    EXPECT_EQ(*p1, 42);
    EXPECT_EQ(pool.available(), 9u);

    {
        auto p2 = pool.acquire();
        *p2 = 100;
        EXPECT_EQ(pool.available(), 8u);
        // p2 离开作用域，归还池
    }

    EXPECT_EQ(pool.available(), 9u);

    // p1 仍持有
    EXPECT_EQ(*p1, 42);
}

// ---- 池耗尽降级 ----
TEST(ObjectPoolTest, PoolExhaustionFallback) {
    ObjectPool<int> pool(2);

    auto p1 = pool.acquire(); // 槽位 0
    auto p2 = pool.acquire(); // 槽位 1
    EXPECT_EQ(pool.available(), 0u);

    // 池耗尽，降级为堆分配
    auto p3 = pool.acquire();
    EXPECT_NE(p3, nullptr);
    *p3 = 999;
    EXPECT_EQ(*p3, 999);
    EXPECT_EQ(pool.available(), 0u);
}

// ---- shared_ptr 删除器归还池 ----
TEST(ObjectPoolTest, SharedPtrDeleterReturnsToPool) {
    ObjectPool<int> pool(3);

    auto p1 = pool.acquire();
    auto* raw_ptr = p1.get();
    EXPECT_EQ(pool.available(), 2u);

    // 手动 reset 触发删除器 → 归还池
    p1.reset();
    EXPECT_EQ(pool.available(), 3u);

    // 重新获取——应得到同一槽位（freelist LIFO）
    auto p2 = pool.acquire();
    EXPECT_EQ(p2.get(), raw_ptr);
}

// ---- 多线程并发获取/释放 ----
TEST(ObjectPoolTest, ConcurrentAcquireRelease) {
    constexpr int kThreads = 4;
    constexpr int kIters = 1000;

    ObjectPool<int> pool(512);

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIters; ++i) {
                auto p = pool.acquire();
                if (!p) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                *p = t * kIters + i;
                // p 离开作用域自动归还
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(errors.load(), 0);
    // 全部归还后池应满
    EXPECT_EQ(pool.available(), 512u);
}

// ---- 全局单例 ----
TEST(ObjectPoolTest, GlobalPoolSingleton) {
    auto& pool1 = GlobalPool<int>::instance();
    auto& pool2 = GlobalPool<int>::instance();
    EXPECT_EQ(&pool1, &pool2);

    auto p = pool1.acquire();
    *p = 7;
    EXPECT_EQ(pool1.available(), pool1.capacity() - 1);
}

// ---- MemoryBlock 专用测试 ----
struct TestBlock {
    uint8_t data[2048];
    uint32_t size = 0;
};

TEST(ObjectPoolTest, MemoryBlockPoolUsage) {
    ObjectPool<TestBlock> pool(128);

    // 模拟捕获循环
    for (int i = 0; i < 100; ++i) {
        auto block = pool.acquire();
        ASSERT_NE(block, nullptr);
        block->size = 1500;
        std::memset(block->data, static_cast<uint8_t>(i), 1500);

        EXPECT_EQ(block->size, 1500u);
        EXPECT_EQ(block->data[0], static_cast<uint8_t>(i));
        // block 自动归还池
    }

    EXPECT_EQ(pool.available(), 128u);
}

// ---- 不同大小池 ----
TEST(ObjectPoolTest, CustomCapacity) {
    ObjectPool<size_t> small_pool(8);
    EXPECT_EQ(small_pool.capacity(), 8u);

    std::vector<ObjectPool<size_t>::PooledPtr> held;
    for (size_t i = 0; i < 8; ++i) {
        held.push_back(small_pool.acquire());
        *held.back() = i;
    }
    EXPECT_EQ(small_pool.available(), 0u);

    // 释放一个，再获取
    held.pop_back(); // 归还
    EXPECT_EQ(small_pool.available(), 1u);

    auto reclaimed = small_pool.acquire();
    EXPECT_NE(reclaimed, nullptr);
    EXPECT_EQ(small_pool.available(), 0u);
}
