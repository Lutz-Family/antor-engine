#include "antor/net/socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace antor::net {

Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::~Socket() {
    close_fd();
}

Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close_fd();
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

void Socket::close_fd() noexcept {
    if (fd_ < 0) {
        return;
    }
    const int fd = std::exchange(fd_, -1);
    while (::close(fd) < 0 && errno == EINTR) {
    }
}

std::uint16_t Socket::local_port() const {
    if (fd_ < 0) {
        throw std::logic_error("socket is closed");
    }
    sockaddr_in addr{};
    socklen_t length = sizeof(addr);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
        throw std::system_error(errno, std::generic_category(), "getsockname");
    }
    return ntohs(addr.sin_port);
}

Socket Socket::listen_tcp(const std::string& host, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }
    Socket socket(fd);
    const int reuse = 1;
    if (::setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("bind host must be an IPv4 address");
    }
    if (::bind(socket.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::system_error(errno, std::generic_category(), "bind");
    }
    if (::listen(socket.fd(), 128) != 0) {
        throw std::system_error(errno, std::generic_category(), "listen");
    }
    return socket;
}

} // namespace antor::net
