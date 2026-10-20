#pragma once

#include "antor/runtime/spsc_queue.hpp"
#include "antor/transport/channel.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace antor::transport {

// Events that cross from a transport worker to the sim thread.
// The producer moves the struct; the consumer owns it afterwards.
// bytes is empty for session open and close. A packet's bytes are an
// unframed FlatBuffers payload. Framing stays inside the transport.
struct InboundEvent {
    enum class Type : std::uint8_t { SessionOpened, SessionClosed, Packet };

    Type type = Type::Packet;
    std::uint64_t session = 0;
    Channel channel{};
    std::vector<std::byte> bytes;
};

// Commands that cross from the sim thread to a transport worker.
struct OutboundCommand {
    enum class Type : std::uint8_t { Send, Disconnect };

    Type type = Type::Send;
    std::uint64_t session = 0;
    Channel channel{};
    std::vector<std::byte> bytes;
};

static_assert(std::is_nothrow_move_assignable_v<InboundEvent>);
static_assert(std::is_nothrow_move_assignable_v<OutboundCommand>);

inline constexpr std::size_t kQueueSlots = 1024;

using InboundQueue = SpscQueue<InboundEvent, kQueueSlots>;
using OutboundQueue = SpscQueue<OutboundCommand, kQueueSlots>;

} // namespace antor::transport
