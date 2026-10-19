#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace antor::zone {

// One simulation partition inside a shard. v0 has a single zone per process.
// Sessions here are ids assigned by the net thread; the zone does not see
// sockets.
class Zone {
public:
    Zone(std::uint32_t id, std::uint32_t shard_id);

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t shard_id() const noexcept { return shard_id_; }

    void add_session(std::uint64_t session);
    void remove_session(std::uint64_t session);
    [[nodiscard]] bool contains(std::uint64_t session) const;
    [[nodiscard]] const std::vector<std::uint64_t>& sessions() const noexcept { return sessions_; }

    void tick(std::chrono::milliseconds dt);
    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::chrono::milliseconds last_dt() const noexcept { return last_dt_; }

private:
    std::uint32_t id_ = 0;
    std::uint32_t shard_id_ = 0;
    std::vector<std::uint64_t> sessions_;
    std::uint64_t ticks_ = 0;
    std::chrono::milliseconds last_dt_{0};
};

} // namespace antor::zone
