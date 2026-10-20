#pragma once

#include "antor/transport/events.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace antor::transport::loopback {

// In-memory transport worker. The calling thread is the transport thread:
// it produces inbound events and consumes outbound commands. No sockets,
// no framing, no demultiplexer. Payloads are the bytes the sim already speaks.
//
// One producer and one consumer per queue, same rule as the TCP worker.
class Endpoint {
public:
    Endpoint(InboundQueue& to_sim, OutboundQueue& from_sim) noexcept
        : to_sim_(to_sim), from_sim_(from_sim) {}

    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    // Queues SessionOpened and returns the new session id. Ids start at 1.
    [[nodiscard]] std::uint64_t open();

    void deliver(std::uint64_t session, Channel channel, std::vector<std::byte> payload);
    void close(std::uint64_t session);

    // Waits until the sim publishes a command or the timeout elapses.
    [[nodiscard]] bool poll(OutboundCommand& out, std::chrono::milliseconds timeout);

private:
    void push(InboundEvent event);

    InboundQueue& to_sim_;
    OutboundQueue& from_sim_;
    std::uint64_t next_ = 1;
};

} // namespace antor::transport::loopback
