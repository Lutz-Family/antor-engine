#pragma once

#include "antor/transport/channel.hpp"
#include "antor/transport/events.hpp"
#include "antor/transport/tcp/owned_fd.hpp"
#include "antor/transport/tcp/socket.hpp"

#include <cstdint>
#include <stop_token>
#include <string>

namespace antor::transport::tcp {

// TCP implements reliable-ordered delivery only. Any other tag closes the
// session so the delivery cannot silently change meaning on this worker.
[[nodiscard]] inline bool is_reliable_ordered(Channel channel) noexcept {
    return channel.delivery == Delivery::ReliableOrdered;
}

// One TCP implementation of the session boundary. poll, sockets, and the
// length prefix live here. The sim thread never includes this header.
//
// bind() runs on the calling thread so a failure is reported before workers
// start and so the listen port is published before the transport thread runs.
// run() is the transport thread.
class TcpPollTransport {
public:
    TcpPollTransport(std::string host, std::uint16_t port);
    ~TcpPollTransport() = default;

    TcpPollTransport(const TcpPollTransport&) = delete;
    TcpPollTransport& operator=(const TcpPollTransport&) = delete;
    TcpPollTransport(TcpPollTransport&& other) noexcept;
    TcpPollTransport& operator=(TcpPollTransport&&) = delete;

    void bind();
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    void run(std::stop_token stop, InboundQueue& inbound, OutboundQueue& outbound);

private:
    std::string host_;
    std::uint16_t requested_port_ = 0;
    std::uint16_t port_ = 0;
    Socket listen_;
    OwnedFd wake_;
    bool bound_ = false;
};

} // namespace antor::transport::tcp
