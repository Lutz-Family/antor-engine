#pragma once

#include "antor/zone/zone.hpp"

#include <cstdint>
#include <span>

namespace antor::zone {

// v0 interest policy: every session in the zone is a recipient of every
// future zone update. The algorithm (grid, spatial hash, rooms) is not chosen.
class InterestManager {
public:
    [[nodiscard]] std::span<const std::uint64_t> recipients(const Zone& zone) const;
};

} // namespace antor::zone
