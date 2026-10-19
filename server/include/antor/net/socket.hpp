#pragma once

#include <cstdint>
#include <string>

namespace antor::net {

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) noexcept;
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] uint16_t local_port() const;

    static Socket listen_tcp(const std::string& host, std::uint16_t port);

private:
    void close_fd() noexcept;

    int fd_ = -1;
};

} // namespace antor::net
