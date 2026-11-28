// SPSCQueue.hpp — bounded, wait-free single-producer/single-consumer queue.
// Storage is allocated once at construction; push/pop never allocate or lock.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace vektur {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    // Producer side. Returns false (and drops the item) when full.
    bool push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;
        slots_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when empty.
    bool pop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        out = slots_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer side: discard everything currently queued.
    void drain() noexcept {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace vektur
