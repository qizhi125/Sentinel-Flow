// Sentinel-Flow — SPSCQueue 吞吐量基准测试
// 目标: 测量单生产者/单消费者队列在不同容量下的传输吞吐量
//
// 运行示例:
//   ./bench_spsc_queue --benchmark_min_time=0.5
//   ./bench_spsc_queue --benchmark_filter=Capacity

#include "sentinel/common/SPSCQueue.h"
#include "sentinel/types/PacketTypes.h"

#include <benchmark/benchmark.h>

#include <atomic>
#include <memory>
#include <thread>

// ============================================================
// 辅助: 模拟报文的 64 字节 POD 结构体（匹配 RawPacket 尺寸）
// ============================================================
struct alignas(64) MockItem {
    int64_t  timestamp_ns;
    uint64_t data[7]; // 填充至 64 字节
};

// ============================================================
// 基准模板: 多线程 Producer → Consumer 吞吐量
// ============================================================
// Capacity 为编译期 SPSC 队列容量，Batch 由 Arg() 运行时传入。
// 每条测试启动 1 Producer 线程 + 1 Consumer 线程，传输 Batch 个
// 元素后测量吞吐量（items/second）。
template <size_t Capacity>
static void BM_SPSCQueue_Throughput(benchmark::State& state) {
    const int64_t batch_size = state.range(0);
    sentinel::SPSCQueue<MockItem, Capacity> queue;

    for (auto _ : state) {
        std::atomic<bool> start{false};

        // Producer 线程: 推送 batch_size 个报文
        std::thread producer([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            MockItem item{};
            for (int64_t i = 0; i < batch_size; ) {
                item.timestamp_ns = i;
                if (queue.push(item)) {
                    ++i;
                }
            }
        });

        // Consumer 线程: 消费全部 batch_size 个报文
        std::thread consumer([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            int64_t received = 0;
            while (received < batch_size) {
                auto item = queue.pop();
                if (item.has_value()) {
                    benchmark::DoNotOptimize(*item);
                    ++received;
                }
            }
        });

        start.store(true, std::memory_order_release);
        producer.join();
        consumer.join();

        state.SetItemsProcessed(batch_size);
    }
}
// 容量扫掠: 2048 / 16384 / 65536
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput, 2048)
    ->Arg(1'000'000)
    ->Name("SPSCQueue/Throughput/Cap2048");
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput, 16384)
    ->Arg(1'000'000)
    ->Name("SPSCQueue/Throughput/Cap16384");
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput, 65536)
    ->Arg(1'000'000)
    ->Name("SPSCQueue/Throughput/Cap65536");

// ============================================================
// 基准: RawPacket 真实负载吞吐量
// ============================================================
// 使用 sentinel::types::RawPacket（含 shared_ptr<MemoryBlock>），
// 模拟生产环境的实际负载尺寸与 move 语义开销。
template <size_t Capacity>
static void BM_SPSCQueue_Throughput_RawPacket(benchmark::State& state) {
    const int64_t batch_size = state.range(0);
    sentinel::SPSCQueue<sentinel::types::RawPacket, Capacity> queue;

    auto block = std::make_shared<sentinel::types::MemoryBlock>();
    block->size = 64;

    for (auto _ : state) {
        std::atomic<bool> start{false};

        std::thread producer([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            for (int64_t i = 0; i < batch_size; ) {
                sentinel::types::RawPacket pkt;
                pkt.kernel_timestamp_ns = i;
                pkt.block = block; // shared_ptr 原子引用计数
                pkt.link_layer_offset = 14;
                if (queue.push(std::move(pkt))) {
                    ++i;
                }
            }
        });

        std::thread consumer([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            int64_t received = 0;
            while (received < batch_size) {
                auto item = queue.pop();
                if (item.has_value()) {
                    benchmark::DoNotOptimize(*item);
                    ++received;
                }
            }
        });

        start.store(true, std::memory_order_release);
        producer.join();
        consumer.join();

        state.SetItemsProcessed(batch_size);
    }
}
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput_RawPacket, 2048)
    ->Arg(500'000)
    ->Name("SPSCQueue/RawPacket/Cap2048");
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput_RawPacket, 16384)
    ->Arg(500'000)
    ->Name("SPSCQueue/RawPacket/Cap16384");
BENCHMARK_TEMPLATE(BM_SPSCQueue_Throughput_RawPacket, 65536)
    ->Arg(500'000)
    ->Name("SPSCQueue/RawPacket/Cap65536");

BENCHMARK_MAIN();
