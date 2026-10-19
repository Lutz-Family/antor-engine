#include "antor/runtime/tick_clock.hpp"
#include "test_harness.hpp"

#include <chrono>

void test_tick_clock() {
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::milliseconds(50);
    antor::TickClock<Clock> clock(period);
    const auto start = Clock::time_point(std::chrono::milliseconds(1000));
    clock.start(start);

    bool clamped = true;
    auto sleep = clock.complete_tick(start, clamped);
    EXPECT(!clamped);
    EXPECT(sleep == period);

    clamped = true;
    sleep = clock.complete_tick(start + std::chrono::milliseconds(60), clamped);
    EXPECT(!clamped);
    EXPECT(sleep == std::chrono::milliseconds(40));

    clamped = false;
    sleep = clock.complete_tick(start + std::chrono::milliseconds(1000), clamped);
    EXPECT(clamped);
    EXPECT(sleep == period);
}
