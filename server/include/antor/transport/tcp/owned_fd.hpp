#pragma once

#include <cerrno>
#include <unistd.h>
#include <utility>

namespace antor::transport::tcp {

class OwnedFd {
public:
    OwnedFd() = default;
    explicit OwnedFd(int fd) noexcept : fd_(fd) {}
    ~OwnedFd() { reset(); }

    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    OwnedFd& operator=(OwnedFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    void reset() noexcept {
        if (fd_ < 0) {
            return;
        }
        const int fd = std::exchange(fd_, -1);
        while (::close(fd) < 0 && errno == EINTR) {
        }
    }

private:
    int fd_ = -1;
};

} // namespace antor::transport::tcp
