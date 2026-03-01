#pragma once
#include <atomic>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>

namespace sentinel::common {

    template<typename T, size_t Capacity = 65536>
    class SPSCQueue {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    public:
        SPSCQueue() : buffer(Capacity) {}

        bool push(T value) {
            size_t h = head.load(std::memory_order_relaxed);
            size_t t = tail.load(std::memory_order_acquire);

            if (((h + 1) & mask) == t) {
                return false;
            }

            buffer[h] = std::move(value);
            head.store((h + 1) & mask, std::memory_order_release);
            return true;
        }

        std::optional<T> popWait(std::chrono::milliseconds timeout) {
            size_t t = tail.load(std::memory_order_relaxed);
            auto start = std::chrono::steady_clock::now();
            int spinCount = 0;
            while (true) {
                size_t h = head.load(std::memory_order_acquire);
                if (t != h) break;
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed >= timeout) return std::nullopt;
                if (spinCount < 1000) {
                    spinCount++;
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    spinCount = 0;
                }
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
        static constexpr size_t mask = Capacity - 1;

        alignas(64) std::atomic<size_t> head{0};
        alignas(64) std::atomic<size_t> tail{0};
    };

} // namespace sentinel::common