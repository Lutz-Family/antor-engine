#pragma once

#include "antor/runtime/spsc_queue.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace antor {

// Messages that cross the net/sim boundary. The producer moves the struct;
// the consumer owns it afterwards. bytes is empty for session open/close
// and for a disconnect request.

struct NetToSim {
    enum class Type : std::uint8_t { SessionOpened, SessionClosed, Packet };

    Type type = Type::Packet;
    std::uint64_t session = 0;
    std::vector<std::byte> bytes;
};

struct SimToNet {
    enum class Type : std::uint8_t { Send, Disconnect };

    Type type = Type::Send;
    std::uint64_t session = 0;
    std::vector<std::byte> bytes;
};

inline constexpr std::size_t kQueueSlots = 1024;

using NetToSimQueue = SpscQueue<NetToSim, kQueueSlots>;
using SimToNetQueue = SpscQueue<SimToNet, kQueueSlots>;

} // namespace antor
