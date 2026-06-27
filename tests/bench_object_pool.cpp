// Sentinel-Flow — ObjectPool 性能基准测试
// 目标: 解析 TD-009 — 量化池耗尽时动态分配回退的性能断层
//
// 运行示例:
//   ./bench_object_pool --benchmark_min_time=1.0
//   ./bench_object_pool --benchmark_filter=ExhaustionCliff

#include "sentinel/common/memory/ObjectPool.h"
#include "sentinel/types/PacketTypes.h"

#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

using namespace sentinel::common;
using namespace sentinel::types;

// ============================================================
// 基准 1: 单线程 acquire/release 基线（CAS 快速路径）
// ============================================================
// 测量从池中取对象并立即释放的延迟。对象通过 shared_ptr
// 析构自动归还（push_free），故每次迭代的池状态完全复位。
static void BM_ObjectPool_AcquireRelease(benchmark::State& state) {
    const size_t pool_capacity = static_cast<size_t>(state.range(0));
    ObjectPool<MemoryBlock> pool(pool_capacity);

    for (auto _ : state) {
        auto ptr = pool.acquire();
        benchmark::DoNotOptimize(ptr);
        // ptr 析构 → 自动归还池 → 下一次 acquire 命中栈槽
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ObjectPool_AcquireRelease)
    ->Arg(2048)->Arg(8192)->Arg(65536)
    ->Name("ObjectPool/AcquireRelease");

// ============================================================
// 基准 2: 池耗尽断层 — 强制 dynamic allocation 回退
// ============================================================
// 预取走池内全部槽位，使 acquire() 进入 new T() + mutex
// 慢速路径。每次迭代测量一次堆分配 + 加锁的延迟。
// 直接量化 TD-009 的性能断层。
static void BM_ObjectPool_ExhaustionCliff(benchmark::State& state) {
    const size_t pool_capacity = static_cast<size_t>(state.range(0));
    ObjectPool<MemoryBlock> pool(pool_capacity);

    // 预占全部槽位，池已空
    std::vector<ObjectPool<MemoryBlock>::PooledPtr> pre_acquired;
    pre_acquired.reserve(pool_capacity);
    for (size_t i = 0; i < pool_capacity; ++i) {
        pre_acquired.push_back(pool.acquire());
    }

    for (auto _ : state) {
        // 池空 → acquire 走 new MemoryBlock() + extra_mutex_ 慢速路径
        auto ptr = pool.acquire();
        benchmark::DoNotOptimize(ptr);
        // ptr 析构执行 delete（非归还池），池保持空状态
    }
    state.SetItemsProcessed(state.iterations());

    // 清理：归还预占槽位（调用方负责，避免 static 析构顺序问题）
    pre_acquired.clear();
}
BENCHMARK(BM_ObjectPool_ExhaustionCliff)
    ->Arg(2048)->Arg(8192)->Arg(65536)
    ->Name("ObjectPool/ExhaustionCliff");

// ============================================================
// 基准 3: 多线程争用吞吐量
// ============================================================
// 多线程同时 acquire/release，测量 Tagged Pointer CAS
// 在竞争下的扩展性。Threads(n) 让 benchmark 框架
// 并发运行 n 个独立的测试循环。
static void BM_ObjectPool_MultiThreaded(benchmark::State& state) {
    const size_t pool_capacity = static_cast<size_t>(state.range(0));
    // static 确保池在所有线程间共享
    static ObjectPool<MemoryBlock> pool(pool_capacity);

    for (auto _ : state) {
        auto ptr = pool.acquire();
        benchmark::DoNotOptimize(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ObjectPool_MultiThreaded)
    ->Arg(4096)
    ->Threads(2)->Threads(4)->Threads(8)
    ->Name("ObjectPool/MultiThreaded");

BENCHMARK_MAIN();
