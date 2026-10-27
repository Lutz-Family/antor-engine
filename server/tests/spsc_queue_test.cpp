#include "antor/runtime/spsc_queue.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <thread>

void test_spsc_queue() {
    antor::SpscQueue<int, 8> queue;
    int value = 0;
    EXPECT(!queue.try_pop(value));
    EXPECT(queue.capacity() == 7);

    for (int i = 0; i < 7; ++i) {
        EXPECT(queue.try_push(int{i}));
    }
    EXPECT(!queue.try_push(99));
    for (int i = 0; i < 7; ++i) {
        EXPECT(queue.try_pop(value));
        EXPECT(value == i);
    }
    EXPECT(!queue.try_pop(value));

    constexpr int kCount = 100000;
    antor::SpscQueue<int, 1024> shared;
    std::atomic<bool> failed{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    std::jthread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!shared.try_push(int{i})) {
                if (std::chrono::steady_clock::now() > deadline) {
                    failed.store(true);
                    return;
                }
                std::this_thread::yield();
            }
        }
    });
    std::jthread consumer([&] {
        for (int expected = 0; expected < kCount; ++expected) {
            int got = -1;
            while (!shared.try_pop(got)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    failed.store(true);
                    return;
                }
                std::this_thread::yield();
            }
            if (got != expected) {
                failed.store(true);
                return;
            }
        }
    });
    producer.join();
    consumer.join();
    EXPECT(!failed.load());
}
