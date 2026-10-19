#include "antor/net/frame.hpp"
#include "antor/protocol/codec.hpp"
#include "antor/protocol/version.hpp"
#include "antor/server.hpp"
#include "test_harness.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

int count_fds() {
    DIR* directory = ::opendir("/proc/self/fd");
    if (directory == nullptr) {
        throw std::runtime_error("opendir /proc/self/fd failed");
    }
    int count = 0;
    while (::readdir(directory) != nullptr) {
        ++count;
    }
    ::closedir(directory);
    return count;
}

class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Fd& operator=(Fd&&) = delete;
    int get() const { return fd_; }

private:
    int fd_ = -1;
};

Fd connect_local(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }
    Fd owned(fd);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        throw std::runtime_error("inet_pton");
    }
    if (::connect(owned.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::system_error(errno, std::generic_category(), "connect");
    }
    return owned;
}

void send_all(int fd, std::span<const std::byte> bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "send");
        }
        sent += static_cast<std::size_t>(n);
    }
}

enum class ReadStatus { Frame, Closed, Timeout };

ReadStatus read_frame(int fd, std::vector<std::byte>& payload, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<std::byte> buffer;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "poll");
        }
        if (ready == 0) {
            return ReadStatus::Timeout;
        }
        std::byte tmp[1024];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "recv");
        }
        if (n == 0) {
            return ReadStatus::Closed;
        }
        buffer.insert(buffer.end(), tmp, tmp + n);
        antor::net::FrameView view;
        const auto status = antor::net::inspect_frame(buffer, view);
        if (status == antor::net::FrameStatus::NeedMore) {
            continue;
        }
        if (status == antor::net::FrameStatus::Error) {
            throw std::runtime_error("bad frame from server");
        }
        payload.assign(view.payload.begin(), view.payload.end());
        return ReadStatus::Frame;
    }
    return ReadStatus::Timeout;
}

void send_hello(int fd, antor::protocol::Codec& codec, std::uint16_t version, std::uint32_t nonce) {
    const auto payload = codec.hello(version, nonce);
    std::vector<std::byte> frame;
    if (!antor::net::write_frame(frame, payload)) {
        throw std::runtime_error("failed to frame hello");
    }
    send_all(fd, frame);
}

antor::ServerConfig test_config() {
    antor::ServerConfig config;
    config.bind_host = "127.0.0.1";
    config.port = 0;
    config.shard_id = 4;
    config.tick = std::chrono::milliseconds(20);
    return config;
}

} // namespace

void test_session() {
    const int fds_before = count_fds();
    {
        antor::Server server(test_config());
        server.start();
        EXPECT(server.port() != 0);

        antor::protocol::Codec codec;
        Fd first = connect_local(server.port());
        send_hello(first.get(), codec, antor::protocol::kProtocolVersion, 0x1111u);
        std::vector<std::byte> payload;
        EXPECT(read_frame(first.get(), payload, std::chrono::milliseconds(2000)) ==
               ReadStatus::Frame);
        const auto first_ack = codec.decode(payload);
        EXPECT(first_ack.kind == antor::protocol::PacketKind::HelloAck);
        EXPECT(first_ack.protocol_version == antor::protocol::kProtocolVersion);
        EXPECT(first_ack.client_nonce == 0x1111u);
        EXPECT(first_ack.server_nonce != 0);

        Fd second = connect_local(server.port());
        send_hello(second.get(), codec, antor::protocol::kProtocolVersion, 0x2222u);
        payload.clear();
        EXPECT(read_frame(second.get(), payload, std::chrono::milliseconds(2000)) ==
               ReadStatus::Frame);
        const auto second_ack = codec.decode(payload);
        EXPECT(second_ack.client_nonce == 0x2222u);
        EXPECT(second_ack.server_nonce != first_ack.server_nonce);

        Fd rejected = connect_local(server.port());
        send_hello(rejected.get(),
                   codec,
                   static_cast<std::uint16_t>(antor::protocol::kProtocolVersion + 1),
                   0x3333u);
        payload.clear();
        EXPECT(read_frame(rejected.get(), payload, std::chrono::milliseconds(2000)) ==
               ReadStatus::Closed);
    }
    EXPECT(count_fds() == fds_before);
}
