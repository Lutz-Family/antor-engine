#include "antor/server.hpp"

#include "antor/net/net_thread.hpp"
#include "antor/sim/sim_thread.hpp"

#include <cerrno>
#include <stdexcept>
#include <sys/eventfd.h>
#include <system_error>
#include <utility>

namespace antor {

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

    net::Socket listen_socket = net::Socket::listen_tcp(config_.bind_host, config_.port);
    const int wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd < 0) {
        throw std::system_error(errno, std::generic_category(), "eventfd");
    }
    net::OwnedFd wake(wake_fd);
    port_ = listen_socket.local_port();

    auto& inbound = to_sim_;
    auto& outbound = to_net_;
    const ServerConfig config = config_;
    sim_.emplace([config, &inbound, &outbound](std::stop_token stop) {
        sim::run_sim(stop, config, inbound, outbound);
    });
    try {
        net_.emplace(
            [listen_socket = std::move(listen_socket), wake = std::move(wake), &inbound, &outbound](
                std::stop_token stop) mutable {
                net::run_net(stop, std::move(listen_socket), std::move(wake), inbound, outbound);
            });
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
