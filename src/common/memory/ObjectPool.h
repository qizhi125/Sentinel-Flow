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

template <typename T>
class ObjectPool {
public:
    ObjectPool() = default;

    explicit ObjectPool(std::size_t preallocate) {
        reserve(preallocate);
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() {
        std::lock_guard<std::mutex> g(allocMutex_);
        for (PoolBlock* b : allBlocks_) {
            if (!b) continue;
            if (b->constructed) b->destroy();
            ::operator delete(b, std::align_val_t(alignof(PoolBlock)));
        }
        allBlocks_.clear();
    }

    T* acquire() {
        PoolBlock* b = popFree_();
        if (b) {
            if (!b->constructed) b->construct();
            return b->objectPtr();
        }
        return allocateOne_()->objectPtr();
    }

    void release(T* obj) noexcept {
        if (!obj) return;
        PoolBlock* b = PoolBlock::fromObjectPtr(obj);
        pushFree_(b);
    }

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
        for (PoolBlock* b : newBlocks) pushFree_(b);
    }

    struct Deleter {
        ObjectPool* pool = nullptr;
        void operator()(T* p) const noexcept {
            if (pool && p) pool->release(p);
        }
    };

    using UniquePtr = std::unique_ptr<T, Deleter>;
    UniquePtr acquireUnique() { return UniquePtr(acquire(), Deleter{this}); }

private:
    struct PoolBlock {
        PoolBlock* next = nullptr;
        bool constructed = false;

        alignas(T) std::byte storage[sizeof(T)];

        T* objectPtr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }

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
            auto* bytes = reinterpret_cast<std::byte*>(obj);
            return reinterpret_cast<PoolBlock*>(bytes - offsetof(PoolBlock, storage));
        }
    };

    struct TaggedHead {
        PoolBlock* ptr = nullptr;
        std::uint64_t tag = 0;
    };

    PoolBlock* allocateRawBlock_() {
        void* mem = ::operator new(sizeof(PoolBlock), std::align_val_t(alignof(PoolBlock)));
        return new (mem) PoolBlock();
    }

    PoolBlock* allocateOne_() {
        std::lock_guard<std::mutex> g(allocMutex_);
        PoolBlock* b = allocateRawBlock_();
        b->construct();
        allBlocks_.push_back(b);
        return b;
    }

    PoolBlock* popFree_() noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        while (head.ptr) {
            PoolBlock* node = head.ptr;
            TaggedHead next{node->next, head.tag + 1};
            if (freeHead_.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                node->next = nullptr;
                return node;
            }
        }
        return nullptr;
    }

    void pushFree_(PoolBlock* node) noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        for (;;) {
            node->next = head.ptr;
            TaggedHead next{node, head.tag + 1};
            if (freeHead_.compare_exchange_weak(head, next, std::memory_order_release, std::memory_order_acquire)) return;
        }
    }

    std::atomic<TaggedHead> freeHead_{TaggedHead{nullptr, 0}};
    std::mutex allocMutex_;
    std::vector<PoolBlock*> allBlocks_;
};