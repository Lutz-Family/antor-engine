#pragma once

#include "antor/net/owned_fd.hpp"
#include "antor/net/socket.hpp"
#include "antor/runtime/messages.hpp"

#include <stop_token>

namespace antor::net {

void run_net(std::stop_token stop,
             Socket listen,
             OwnedFd wake,
             NetToSimQueue& inbound,
             SimToNetQueue& outbound);

} // namespace antor::net
