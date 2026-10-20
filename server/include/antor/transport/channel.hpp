#pragma once

#include <cstdint>

namespace antor::transport {

// How a payload should travel. The tag is part of the session boundary so a
// later datagram transport can be added without changing SessionBus::send.
// Only ReliableOrdered has an implementation today.
enum class Delivery : std::uint8_t {
    // Lossless and ordered for one session. Sequence is unused and stays 0.
    ReliableOrdered = 0,
    // May be dropped. A newer sequence supersedes an older one for that
    // session. No worker implements this yet.
    UnreliableSequenced = 1,
};

struct Channel {
    Delivery delivery = Delivery::ReliableOrdered;
    // Meaningful for UnreliableSequenced. Ignored for ReliableOrdered.
    std::uint32_t sequence = 0;

    static constexpr Channel reliable() noexcept { return {}; }

    static constexpr Channel sequenced(std::uint32_t sequence) noexcept {
        return Channel{Delivery::UnreliableSequenced, sequence};
    }

    friend constexpr bool operator==(Channel left, Channel right) noexcept {
        return left.delivery == right.delivery && left.sequence == right.sequence;
    }
};

} // namespace antor::transport
