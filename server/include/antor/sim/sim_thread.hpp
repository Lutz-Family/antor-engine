#pragma once

#include "antor/config.hpp"
#include "antor/runtime/messages.hpp"

#include <stop_token>

namespace antor::sim {

void run_sim(std::stop_token stop,
             const ServerConfig& config,
             NetToSimQueue& inbound,
             SimToNetQueue& outbound);

} // namespace antor::sim
