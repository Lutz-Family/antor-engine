#include "antor/zone/interest.hpp"

namespace antor::zone {

std::span<const std::uint64_t> InterestManager::recipients(const Zone& zone) const {
    // Placeholder policy: no spatial filter until an interest algorithm is chosen.
    return zone.sessions();
}

} // namespace antor::zone
