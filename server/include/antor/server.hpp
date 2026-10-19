#pragma once

#include "antor/config.hpp"
#include "antor/runtime/messages.hpp"

#include <cstdint>
#include <optional>
#include <thread>

namespace antor {

// Owns the listen lifecycle. After start(), the net thread owns sockets and
// the sim thread owns zone state. This object only shares the two queues and
// the stop tokens with those threads.
class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    void start();
    void request_stop();
    void join();

    // Valid after start(). Published before either worker runs.
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
    [[nodiscard]] const ServerConfig& config() const noexcept { return config_; }

private:
    ServerConfig config_;
    std::uint16_t port_ = 0;
    bool started_ = false;
    NetToSimQueue to_sim_;
    SimToNetQueue to_net_;
    std::optional<std::jthread> sim_;
    std::optional<std::jthread> net_;
};

} // namespace antor
