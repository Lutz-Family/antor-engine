#include "antor/server.hpp"

#include "antor/sim/sim_thread.hpp"
#include "antor/transport/tcp/poll_transport.hpp"

#include <stdexcept>
#include <utility>

namespace antor {

// Composition root. run_sim speaks the session bus only. The worker installed
// here is the TCP poll transport. Swapping it for another worker (the
// loopback endpoint in tests, a future datagram transport) does not change
// the sim thread.
Server::Server(ServerConfig config) : config_(std::move(config)) {}

Server::~Server() {
    request_stop();
    join();
}

void Server::start() {
    if (started_) {
        throw std::logic_error("server already started");
    }
    if (config_.tick.count() <= 0) {
        throw std::invalid_argument("tick must be positive");
    }

    transport::tcp::TcpPollTransport transport(config_.bind_host, config_.port);
    transport.bind();
    port_ = transport.port();

    auto& inbound = to_sim_;
    auto& outbound = to_net_;
    const ServerConfig config = config_;
    sim_.emplace([config, &inbound, &outbound](std::stop_token stop) {
        sim::run_sim(stop, config, inbound, outbound);
    });
    try {
        net_.emplace([transport = std::move(transport), &inbound, &outbound](
                         std::stop_token stop) mutable { transport.run(stop, inbound, outbound); });
    } catch (...) {
        if (sim_) {
            sim_->request_stop();
            sim_->join();
            sim_.reset();
        }
        port_ = 0;
        throw;
    }
    started_ = true;
}

void Server::request_stop() {
    if (net_) {
        net_->request_stop();
    }
    if (sim_) {
        sim_->request_stop();
    }
}

void Server::join() {
    if (net_) {
        net_->join();
        net_.reset();
    }
    if (sim_) {
        sim_->join();
        sim_.reset();
    }
}

} // namespace antor
