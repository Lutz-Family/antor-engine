#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace antor {

// Bounded single-producer, single-consumer queue.
//
// One slot is left empty so head == tail means empty. Usable capacity is
// Capacity - 1. push moves into the reserved slot only after the queue is
// known to have room, so a failed push leaves the caller's object intact.
//
// The only mutable cross-thread state is the head and tail atomics plus the
// slot each side exclusively owns between those publishes.
template <typename T, std::size_t Capacity> class SpscQueue {
public:
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two and at least 2");
    static_assert(std::is_nothrow_move_assignable_v<T>);

    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    [[nodiscard]] bool try_push(T&& item) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = (tail + 1) & kMask;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(slots_[head]);
        head_.store((head + 1) & kMask, std::memory_order_release);
        return true;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) T slots_[Capacity]{};
};

} // namespace antor
