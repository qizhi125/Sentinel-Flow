#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

template <typename T> class ObjectPool {
public:
    ObjectPool() = default;
    explicit ObjectPool(std::size_t preallocate) {
        reserve(preallocate);
    }

    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* block : allBlocks_) {
            if (block->constructed) {
                reinterpret_cast<T*>(block->storage)->~T();
            }
            delete block;
        }
    }

    T* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!freeList_.empty()) {
            auto* block = freeList_.back();
            freeList_.pop_back();
            if (!block->constructed) {
                new (block->storage) T();
                block->constructed = true;
            }
            return reinterpret_cast<T*>(block->storage);
        }
        auto* block = new PoolBlock();
        new (block->storage) T();
        block->constructed = true;
        allBlocks_.push_back(block);
        return reinterpret_cast<T*>(block->storage);
    }

    void release(T* obj) noexcept {
        if (!obj)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto* block = reinterpret_cast<PoolBlock*>(reinterpret_cast<char*>(obj) - offsetof(PoolBlock, storage));
        freeList_.push_back(block);
    }

    void reserve(std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < n; ++i) {
            auto* block = new PoolBlock();
            allBlocks_.push_back(block);
            freeList_.push_back(block);
        }
    }

private:
    struct PoolBlock {
        bool constructed = false;
        alignas(T) char storage[sizeof(T)];
    };

    std::mutex mutex_;
    std::vector<PoolBlock*> allBlocks_;
    std::vector<PoolBlock*> freeList_;
};
