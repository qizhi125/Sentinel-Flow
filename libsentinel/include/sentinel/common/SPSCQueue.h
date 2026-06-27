#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

namespace sentinel {

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // 生产者：将元素推入队列。队满返回 false。
    //
    // 提供 const& 和 && 两个重载，避免大尺寸/高对齐（如 alignas(64)）
    // 结构体按值传递时的 GCC ABI 警告。
    bool push(const T& item) {
        size_t const h = head_.load(std::memory_order_relaxed);
        size_t const t = tail_.load(std::memory_order_acquire);

        if (((h + 1) & kMask) == t) {
            return false;
        }

        buffer_[h] = item;
        head_.store((h + 1) & kMask, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        size_t const h = head_.load(std::memory_order_relaxed);
        size_t const t = tail_.load(std::memory_order_acquire);

        if (((h + 1) & kMask) == t) {
            return false;
        }

        buffer_[h] = std::move(item);
        head_.store((h + 1) & kMask, std::memory_order_release);
        return true;
    }

    // 消费者：从队列取出元素。队空返回 std::nullopt。
    std::optional<T> pop() {
        size_t const t = tail_.load(std::memory_order_relaxed);
        size_t const h = head_.load(std::memory_order_acquire);

        if (t == h) {
            return std::nullopt;
        }

        T item = std::move(buffer_[t]);
        tail_.store((t + 1) & kMask, std::memory_order_release);
        return item;
    }

    // 消费者：阻塞等待直到有元素可取出。
    // 调用方需确保有生产者持续推送，否则会忙等。
    T popWait() {
        for (;;) {
            auto item = pop();
            if (item.has_value()) {
                return std::move(*item);
            }
        }
    }

    // 队列中当前元素数量（近似值，跨线程调用无精确语义）。
    size_t size() const {
        size_t const h = head_.load(std::memory_order_acquire);
        size_t const t = tail_.load(std::memory_order_acquire);
        if (h >= t) {
            return h - t;
        }
        return Capacity - t + h;
    }

    // 队列是否为空（近似值）。
    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

    static constexpr size_t capacity() {
        return Capacity;
    }
};

} // namespace sentinel
