#include "antor/transport/loopback.hpp"

#include <stdexcept>
#include <thread>
#include <utility>

namespace antor::transport::loopback {
namespace {

constexpr std::chrono::milliseconds kPushTimeout{2000};
constexpr std::chrono::milliseconds kSpin{1};

} // namespace

void Endpoint::push(InboundEvent event) {
    const auto deadline = std::chrono::steady_clock::now() + kPushTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (to_sim_.try_push(std::move(event))) {
            return;
        }
        std::this_thread::sleep_for(kSpin);
    }
    if (!to_sim_.try_push(std::move(event))) {
        throw std::runtime_error("loopback inbound queue stayed full");
    }
}

std::uint64_t Endpoint::open() {
    const auto session = next_++;
    InboundEvent event;
    event.type = InboundEvent::Type::SessionOpened;
    event.session = session;
    event.channel = Channel::reliable();
    push(std::move(event));
    return session;
}

void Endpoint::deliver(std::uint64_t session, Channel channel, std::vector<std::byte> payload) {
    InboundEvent event;
    event.type = InboundEvent::Type::Packet;
    event.session = session;
    event.channel = channel;
    event.bytes = std::move(payload);
    push(std::move(event));
}

void Endpoint::close(std::uint64_t session) {
    InboundEvent event;
    event.type = InboundEvent::Type::SessionClosed;
    event.session = session;
    push(std::move(event));
}

bool Endpoint::poll(OutboundCommand& out, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (from_sim_.try_pop(out)) {
            return true;
        }
        std::this_thread::sleep_for(kSpin);
    }
    return from_sim_.try_pop(out);
}

} // namespace antor::transport::loopback
