#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace sentinel::common {

// 无锁对象池。
//
// 预分配固定大小的 Slot 数组，通过 Tagged Pointer CAS 构建无锁空闲链表。
// acquire() 返回 std::shared_ptr<T> —— 引用计数归零时自动归还对象到池。
// 池耗尽时降级为普通堆分配（持有 mutex，非热路径）。
//
// 线程安全：多生产者/多消费者。
template <typename T>
class ObjectPool {
    static constexpr size_t kDefaultCapacity = 8192;

    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot* next = nullptr;
    };

    struct alignas(16) TaggedHead {
        Slot* ptr = nullptr;
        uint64_t tag = 0;
    };

public:
    // 自定义 shared_ptr 删除器：归还对象到池。
    struct Deleter {
        ObjectPool* pool;

        void operator()(T* obj) const noexcept {
            if (pool && obj) {
                pool->release(obj);
            }
        }
    };

    using PooledPtr = std::shared_ptr<T>;

    explicit ObjectPool(size_t capacity = kDefaultCapacity)
        : slots_(capacity) {
        for (auto& slot : slots_) {
            push_free(&slot);
        }
    }

    ~ObjectPool() {
        // 等待所有借出对象归还
        // 注：外部持有的 shared_ptr 生命周期可能长于池析构，
        // 调用方需确保池在全部 PooledPtr 释放后才析构。
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    // 从池获取对象。
    // 返回 shared_ptr<T> —— 最后一个引用销毁时自动归还对象到池。
    [[nodiscard]] PooledPtr acquire() {
        Slot* slot = pop_free();
        if (slot) {
            T* obj = reinterpret_cast<T*>(slot->storage);
            return PooledPtr(obj, Deleter{this});
        }

        // 池耗尽 — 降级为堆分配（非热路径）
        auto* obj = new T();
        {
            std::lock_guard lock(extra_mutex_);
            extra_allocations_.push_back(obj);
        }
        return PooledPtr(obj, [](T* p) { delete p; });
    }

    // 可用槽位数（近似值）。
    [[nodiscard]] size_t available() const noexcept {
        return available_.load(std::memory_order_relaxed);
    }

    // 总容量。
    [[nodiscard]] size_t capacity() const noexcept {
        return slots_.size();
    }

private:
    void release(T* obj) noexcept {
        Slot* slot = slot_from_ptr(obj);
        push_free(slot);
    }

    [[nodiscard]] static Slot* slot_from_ptr(T* obj) noexcept {
        return reinterpret_cast<Slot*>(
            reinterpret_cast<std::byte*>(obj) - offsetof(Slot, storage));
    }

    // ---- 无锁空闲链表 ----

    Slot* pop_free() noexcept {
        TaggedHead head = free_head_.load(std::memory_order_acquire);
        while (head.ptr) {
            Slot* node = head.ptr;
            TaggedHead next{node->next, head.tag + 1};
            if (free_head_.compare_exchange_weak(head, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                node->next = nullptr;
                available_.fetch_sub(1, std::memory_order_relaxed);
                return node;
            }
        }
        return nullptr;
    }

    void push_free(Slot* node) noexcept {
        TaggedHead head = free_head_.load(std::memory_order_acquire);
        for (;;) {
            node->next = head.ptr;
            TaggedHead next{node, head.tag + 1};
            if (free_head_.compare_exchange_weak(head, next,
                    std::memory_order_release, std::memory_order_acquire)) {
                available_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    std::vector<Slot> slots_;
    alignas(64) std::atomic<TaggedHead> free_head_{};
    alignas(64) std::atomic<size_t> available_{0};

    // 降级分配追踪（非热路径）
    std::vector<T*> extra_allocations_;
    std::mutex extra_mutex_;
};

// 全局报文内存池。
// 单例模式 — 捕获线程和管线共享同一池。
template <typename T>
class GlobalPool {
public:
    static ObjectPool<T>& instance() {
        static ObjectPool<T> pool(8192);
        return pool;
    }

private:
    GlobalPool() = delete;
};

} // namespace sentinel::common
