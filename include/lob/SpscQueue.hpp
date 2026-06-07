#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace lob {

template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2, "SPSC queue capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "SPSC queue capacity must be a power of two");
    static_assert(std::is_default_constructible_v<T>, "SPSC queue entries must be default constructible");

public:
    [[nodiscard]] bool try_push(const T& item) {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& item) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) & (Capacity - 1);
    }

    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace lob
