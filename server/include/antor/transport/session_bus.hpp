#pragma once

#include "antor/transport/events.hpp"

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace antor::transport {

// Sim-thread endpoint of the session boundary.
//
// Concrete on purpose. send() and try_pop() inline to one SPSC operation.
// Channel rides inside the queue message. There is no vtable and no
// allocation beyond the payload vector the caller already owns.
//
// Not thread-safe. The sim thread owns this object. The transport thread
// uses the queues directly and never calls in here.
class SessionBus {
public:
    SessionBus(InboundQueue& inbound, OutboundQueue& outbound) noexcept
        : inbound_(inbound), outbound_(outbound) {}

    SessionBus(const SessionBus&) = delete;
    SessionBus& operator=(const SessionBus&) = delete;
    SessionBus(SessionBus&&) = delete;
    SessionBus& operator=(SessionBus&&) = delete;

    [[nodiscard]] bool try_pop(InboundEvent& event) { return inbound_.try_pop(event); }

    // Moves bytes into a command. A full queue keeps the command on the
    // private overflow deque, in order, instead of dropping it or blocking.
    void send(std::uint64_t session, Channel channel, std::vector<std::byte> bytes) {
        OutboundCommand command;
        command.type = OutboundCommand::Type::Send;
        command.session = session;
        command.channel = channel;
        command.bytes = std::move(bytes);
        enqueue(std::move(command));
    }

    void disconnect(std::uint64_t session) {
        OutboundCommand command;
        command.type = OutboundCommand::Type::Disconnect;
        command.session = session;
        enqueue(std::move(command));
    }

    // Drains overflow into the outbound queue. Stops at the first full push
    // so a later command cannot overtake an earlier one.
    void flush() {
        while (!pending_.empty()) {
            if (!outbound_.try_push(std::move(pending_.front()))) {
                return;
            }
            pending_.pop_front();
        }
    }

private:
    void enqueue(OutboundCommand command) {
        if (!pending_.empty() || !outbound_.try_push(std::move(command))) {
            pending_.push_back(std::move(command));
        }
        flush();
    }

    InboundQueue& inbound_;
    OutboundQueue& outbound_;
    std::deque<OutboundCommand> pending_;
};

} // namespace antor::transport
