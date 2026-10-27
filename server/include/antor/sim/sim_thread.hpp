#pragma once

#include "antor/config.hpp"
#include "antor/transport/events.hpp"

#include <stop_token>

namespace antor::sim {

// Drives the fixed tick. The queues are the transport boundary; this function
// does not name a socket, a frame, or a demultiplexer.
void run_sim(std::stop_token stop,
             const ServerConfig& config,
             transport::InboundQueue& inbound,
             transport::OutboundQueue& outbound);

} // namespace antor::sim
