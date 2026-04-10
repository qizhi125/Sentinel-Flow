#include <gtest/gtest.h>
#include "common/queues/SPSCQueue.h"
#include "common/memory/ObjectPool.h"
#include <thread>
#include <vector>

using namespace sentinel::common;

// 测试 1：SPSCQueue 无锁队列的高并发读写一致性
TEST(SPSCQueueTest, ConcurrentPushPop) {
    SPSCQueue<int> queue(1024);
    const int num_items = 50000;
    long long sum_pushed = 0;
    long long sum_popped = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= num_items; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield(); // 队满则等待
            }
            sum_pushed += i;
        }
    });

    std::thread consumer([&]() {
        for (int i = 1; i <= num_items; ++i) {
            while (true) {
                auto val = queue.popWait(std::chrono::milliseconds(10));
                if (val) {
                    sum_popped += *val;
                    break;
                }
            }
        }
    });

    producer.join();
    consumer.join();

    // 验证所有生产的数据都被正确消费，没有丢失或错乱
    EXPECT_EQ(sum_pushed, sum_popped);
    EXPECT_EQ(queue.size(), 0);
}

// 测试 2：ObjectPool 内存池的复用机制
TEST(ObjectPoolTest, AcquireAndRelease) {
    ObjectPool<int> pool(10); // 预分配 10 个
    
    int* p1 = pool.acquire();
    *p1 = 42;
    EXPECT_NE(p1, nullptr);

    int* p2 = pool.acquire();
    *p2 = 100;
    EXPECT_NE(p1, p2);

    pool.release(p1);
    
    // p3 应该完美复用 p1 刚刚释放的内存地址
    int* p3 = pool.acquire();
    EXPECT_EQ(p1, p3); 
    
    pool.release(p2);
    pool.release(p3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
