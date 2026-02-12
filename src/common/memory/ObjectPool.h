#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

// Sentinel-Flow Phase 3
// ObjectPool<T>: MPMC object pool with a lock-free free-list (ABA-protected via tag).
//
// Notes:
// - The pool returns already-constructed T*. It does NOT reset/zero the object.
// - Destructors are called only when the pool is destroyed.
// - To avoid any allocations in the hot path, call reserve(N) during init.
//
// Logging policy reminder (project-wide):
// - UI: Simplified Chinese only
// - Logs: English only

template <typename T>
class ObjectPool {
public:
    ObjectPool() = default;

    explicit ObjectPool(std::size_t preallocate) {
        reserve(preallocate);
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    ~ObjectPool() {
        // Best-effort: caller should ensure no concurrent acquire/release during destruction.
        std::lock_guard<std::mutex> g(allocMutex_);
        for (PoolBlock* b : allBlocks_) {
            if (!b) continue;
            // Destroy T and free the block.
            b->destroy();
            ::operator delete(b, std::align_val_t(alignof(PoolBlock)));
        }
        allBlocks_.clear();
    }

    // Get an object: reuse from pool or allocate a new one if empty.
    // Thread-safe (MPMC).
    T* acquire() {
        PoolBlock* b = popFree_();
        if (b) {
            return b->objectPtr();
        }
        // Slow path: allocate a new block.
        return allocateOne_()->objectPtr();
    }

    // Return an object to the pool.
    // Thread-safe (MPMC). `obj` must come from THIS pool.
    void release(T* obj) noexcept {
        if (!obj) return;
        PoolBlock* b = PoolBlock::fromObjectPtr(obj);
        pushFree_(b);
    }

    // Pre-allocate N objects so acquire()/release() will not allocate in the hot path.
    // Thread-safe; intended for init time.
    void reserve(std::size_t n) {
        if (n == 0) return;
        std::vector<PoolBlock*> newBlocks;
        newBlocks.reserve(n);

        {
            std::lock_guard<std::mutex> g(allocMutex_);
            for (std::size_t i = 0; i < n; ++i) {
                PoolBlock* b = allocateRawBlock_();
                b->construct();
                allBlocks_.push_back(b);
                newBlocks.push_back(b);
            }
        }

        // Push outside the allocation lock to reduce contention.
        for (PoolBlock* b : newBlocks) {
            pushFree_(b);
        }
    }

    // Convenience RAII wrapper (optional).
    struct Deleter {
        ObjectPool* pool = nullptr;
        void operator()(T* p) const noexcept {
            if (pool && p) pool->release(p);
        }
    };

    using UniquePtr = std::unique_ptr<T, Deleter>;

    UniquePtr acquireUnique() {
        return UniquePtr(acquire(), Deleter{this});
    }

private:
    struct PoolBlock {
        PoolBlock* next = nullptr; // free-list next
        bool constructed = false;

        // Storage for T (constructed via placement-new).
        alignas(T) std::byte storage[sizeof(T)];

        T* objectPtr() noexcept {
            return std::launder(reinterpret_cast<T*>(storage));
        }
        const T* objectPtr() const noexcept {
            return std::launder(reinterpret_cast<const T*>(storage));
        }

        void construct() {
            if (constructed) return;
            ::new (static_cast<void*>(storage)) T();
            constructed = true;
        }

        void destroy() noexcept {
            if (!constructed) return;
            objectPtr()->~T();
            constructed = false;
        }

        static PoolBlock* fromObjectPtr(T* obj) noexcept {
            // PoolBlock::storage is the last member; `obj` points inside it.
            auto* bytes = reinterpret_cast<std::byte*>(obj);
            return reinterpret_cast<PoolBlock*>(bytes - offsetof(PoolBlock, storage));
        }
    };

    struct TaggedHead {
        PoolBlock* ptr = nullptr;
        std::uint64_t tag = 0;
    };

    static_assert(std::is_trivially_copyable_v<TaggedHead>,
                  "TaggedHead must be trivially copyable for std::atomic");

    PoolBlock* allocateRawBlock_() {
        void* mem = ::operator new(sizeof(PoolBlock), std::align_val_t(alignof(PoolBlock)));
        // Value-initialize next/constructed.
        return new (mem) PoolBlock();
    }

    PoolBlock* allocateOne_() {
        PoolBlock* b = nullptr;
        {
            std::lock_guard<std::mutex> g(allocMutex_);
            b = allocateRawBlock_();
            b->construct();
            allBlocks_.push_back(b);
        }
        return b;
    }

    PoolBlock* popFree_() noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        while (head.ptr) {
            PoolBlock* node = head.ptr;
            TaggedHead next;
            next.ptr = node->next;
            next.tag = head.tag + 1;
            if (freeHead_.compare_exchange_weak(
                    head, next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                node->next = nullptr;
                return node;
            }
            // CAS failed: `head` has been updated with the current value; retry.
        }
        return nullptr;
    }

    void pushFree_(PoolBlock* node) noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        for (;;) {
            node->next = head.ptr;
            TaggedHead next{node, head.tag + 1};
            if (freeHead_.compare_exchange_weak(
                    head, next,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                return;
            }
        }
    }

private:
    std::atomic<TaggedHead> freeHead_{TaggedHead{nullptr, 0}};

    // Tracks all blocks for destruction; only touched on slow path (reserve/allocate/dtor).
    std::mutex allocMutex_;
    std::vector<PoolBlock*> allBlocks_;
};

