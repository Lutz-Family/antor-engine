#include "antor/zone/zone.hpp"

#include <algorithm>

namespace antor::zone {

Zone::Zone(std::uint32_t id, std::uint32_t shard_id) : id_(id), shard_id_(shard_id) {}

void Zone::add_session(std::uint64_t session) {
    if (!contains(session)) {
        sessions_.push_back(session);
    }
}

void Zone::remove_session(std::uint64_t session) {
    const auto found = std::find(sessions_.begin(), sessions_.end(), session);
    if (found != sessions_.end()) {
        sessions_.erase(found);
    }
}

bool Zone::contains(std::uint64_t session) const {
    return std::find(sessions_.begin(), sessions_.end(), session) != sessions_.end();
}

void Zone::tick(std::chrono::milliseconds dt) {
    last_dt_ = dt;
    ++ticks_;
}

} // namespace antor::zone
