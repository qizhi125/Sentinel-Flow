#pragma once
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <optional>
#include <thread>
#include <vector>

namespace sentinel::common {

template <typename T> class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity = 65536) : buffer(capacity), mask(capacity - 1) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("SPSCQueue capacity must be >= 2 and a power of two");
        }
    }

    bool push(T value) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        if (((h + 1) & mask) == t)
            return false;
        buffer[h] = std::move(value);
        head.store((h + 1) & mask, std::memory_order_release);
        return true;
    }

    std::optional<T> popWait(std::chrono::milliseconds timeout) {
        size_t t = tail.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        while (true) {
            size_t h = head.load(std::memory_order_acquire);
            if (t != h)
                break;
            if (std::chrono::steady_clock::now() - start >= timeout)
                return std::nullopt;
            std::this_thread::yield();
        }
        T value = std::move(buffer[t]);
        tail.store((t + 1) & mask, std::memory_order_release);
        return value;
    }

    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        return (h - t) & mask;
    }

private:
    std::vector<T> buffer;
    const size_t mask;

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
};

} // namespace sentinel::common