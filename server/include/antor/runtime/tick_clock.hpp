#pragma once

#include <chrono>

namespace antor {

// Fixed-step clock. complete_tick() is called after a tick has run.
//
// The step passed to gameplay is always the nominal period, including when
// the wall clock was late. If the tick finished more than one period after
// its deadline, missed steps are dropped and the next deadline is one period
// from now. There is no catch-up burst.
template <typename Clock = std::chrono::steady_clock> class TickClock {
public:
    using TimePoint = typename Clock::time_point;
    using Duration = typename Clock::duration;

    explicit TickClock(Duration period) : period_(period) {}

    void start(TimePoint now) { next_ = now; }

    Duration complete_tick(TimePoint now, bool& clamped) {
        next_ += period_;
        if (now <= next_) {
            clamped = false;
            return next_ - now;
        }
        clamped = true;
        next_ = now + period_;
        return period_;
    }

    Duration period() const { return period_; }

private:
    Duration period_;
    TimePoint next_{};
};

} // namespace antor
