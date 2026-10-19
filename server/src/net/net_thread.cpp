#include "antor/net/net_thread.hpp"

#include "antor/net/frame.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <optional>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace antor::net {
namespace {

constexpr std::size_t kMaxConnections = 1024;
constexpr std::size_t kMaxQueuedOutbound = 1024 * 1024;
constexpr int kPollTimeoutMs = 200;

struct Connection {
    Socket socket;
    std::uint64_t session = 0;
    std::vector<std::byte> inbound;
    std::size_t in_pos = 0;
    std::vector<std::byte> outbound;
    std::size_t out_pos = 0;
    std::optional<NetToSim> pending;
    bool closing = false;
};

void begin_close(Connection& connection) {
    if (connection.closing) {
        return;
    }
    connection.closing = true;
    connection.socket = Socket{};
    connection.inbound.clear();
    connection.in_pos = 0;
    connection.outbound.clear();
    connection.out_pos = 0;
    NetToSim closed;
    closed.type = NetToSim::Type::SessionClosed;
    closed.session = connection.session;
    connection.pending = std::move(closed);
}

bool flush_pending(Connection& connection, NetToSimQueue& inbound) {
    if (!connection.pending) {
        return true;
    }
    if (!inbound.try_push(std::move(*connection.pending))) {
        return false;
    }
    connection.pending.reset();
    return true;
}

std::span<const std::byte> unread_inbound(const Connection& connection) {
    if (connection.in_pos > connection.inbound.size()) {
        return {};
    }
    const auto size = connection.inbound.size() - connection.in_pos;
    if (size == 0) {
        return {};
    }
    return std::span<const std::byte>(connection.inbound.data() + connection.in_pos, size);
}

void compact_inbound(Connection& connection) {
    if (connection.in_pos == 0) {
        return;
    }
    if (connection.in_pos >= connection.inbound.size()) {
        connection.inbound.clear();
        connection.in_pos = 0;
        return;
    }
    if (connection.in_pos > 4096) {
        connection.inbound.erase(connection.inbound.begin(),
                                 connection.inbound.begin() +
                                     static_cast<std::ptrdiff_t>(connection.in_pos));
        connection.in_pos = 0;
    }
}

void ingest(Connection& connection, NetToSimQueue& inbound) {
    if (connection.closing || connection.pending) {
        return;
    }
    while (!connection.pending && !connection.closing) {
        FrameView frame;
        const auto status = inspect_frame(unread_inbound(connection), frame);
        if (status == FrameStatus::NeedMore) {
            break;
        }
        if (status == FrameStatus::Error) {
            begin_close(connection);
            return;
        }
        NetToSim message;
        message.type = NetToSim::Type::Packet;
        message.session = connection.session;
        message.bytes.assign(frame.payload.begin(), frame.payload.end());
        connection.in_pos += frame.consumed;
        if (!inbound.try_push(std::move(message))) {
            connection.pending = std::move(message);
            return;
        }
    }
    compact_inbound(connection);
}

void read_socket(Connection& connection, NetToSimQueue& inbound) {
    if (connection.closing || connection.pending || !connection.socket.valid()) {
        return;
    }
    std::byte tmp[4096];
    while (connection.socket.valid()) {
        // Stop once a full maximum frame is buffered so one burst cannot grow
        // the connection without bound. ingest() below parses what we have.
        if (unread_inbound(connection).size() >= kMaxPayloadBytes + kFrameHeaderBytes) {
            break;
        }
        const ssize_t n = ::recv(connection.socket.fd(), tmp, sizeof(tmp), 0);
        if (n > 0) {
            connection.inbound.insert(connection.inbound.end(), tmp, tmp + n);
            continue;
        }
        if (n == 0) {
            begin_close(connection);
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        begin_close(connection);
        break;
    }
    ingest(connection, inbound);
}

void write_socket(Connection& connection) {
    if (connection.closing || !connection.socket.valid()) {
        return;
    }
    while (connection.out_pos < connection.outbound.size()) {
        const auto* data = connection.outbound.data() + connection.out_pos;
        const auto remaining = connection.outbound.size() - connection.out_pos;
        const ssize_t n = ::send(connection.socket.fd(), data, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            connection.out_pos += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        begin_close(connection);
        return;
    }
    if (!connection.outbound.empty() && connection.out_pos >= connection.outbound.size()) {
        connection.outbound.clear();
        connection.out_pos = 0;
    }
}

std::optional<std::size_t> find_session(const std::vector<Connection>& connections,
                                        std::uint64_t session) {
    for (std::size_t i = 0; i < connections.size(); ++i) {
        if (connections[i].session == session) {
            return i;
        }
    }
    return std::nullopt;
}

void apply_outbound(std::vector<Connection>& connections, SimToNetQueue& outbound) {
    SimToNet message;
    while (outbound.try_pop(message)) {
        const auto index = find_session(connections, message.session);
        if (!index) {
            continue;
        }
        Connection& connection = connections[*index];
        if (message.type == SimToNet::Type::Disconnect || connection.closing) {
            begin_close(connection);
            continue;
        }
        if (!write_frame(connection.outbound, message.bytes) ||
            connection.outbound.size() > kMaxQueuedOutbound) {
            begin_close(connection);
        }
    }
}

void sweep(std::vector<Connection>& connections) {
    std::size_t index = 0;
    while (index < connections.size()) {
        if (connections[index].closing && !connections[index].pending) {
            connections[index] = std::move(connections.back());
            connections.pop_back();
            continue;
        }
        ++index;
    }
}

void accept_clients(Socket& listen,
                    std::vector<Connection>& connections,
                    std::uint64_t& next_session) {
    while (connections.size() < kMaxConnections) {
        const int fd = ::accept4(listen.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            return;
        }
        const int nodelay = 1;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0) {
            while (::close(fd) < 0 && errno == EINTR) {
            }
            continue;
        }
        Connection connection;
        connection.socket = Socket(fd);
        connection.session = next_session++;
        NetToSim opened;
        opened.type = NetToSim::Type::SessionOpened;
        opened.session = connection.session;
        connection.pending = std::move(opened);
        connections.push_back(std::move(connection));
    }
    while (true) {
        const int fd = ::accept4(listen.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return;
        }
        while (::close(fd) < 0 && errno == EINTR) {
        }
    }
}

void drain_wake(int wake_fd) {
    std::uint64_t value = 0;
    while (true) {
        const ssize_t n = ::read(wake_fd, &value, sizeof(value));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

} // namespace

void run_net(std::stop_token stop,
             Socket listen,
             OwnedFd wake,
             NetToSimQueue& inbound,
             SimToNetQueue& outbound) {
    std::vector<Connection> connections;
    connections.reserve(64);
    std::vector<pollfd> pollfds;
    pollfds.reserve(66);
    std::uint64_t next_session = 1;

    const std::stop_callback wake_on_stop(stop, [&wake] {
        const std::uint64_t one = 1;
        const ssize_t written = ::write(wake.get(), &one, sizeof(one));
        static_cast<void>(written);
    });

    while (!stop.stop_requested()) {
        for (auto& connection : connections) {
            flush_pending(connection, inbound);
            ingest(connection, inbound);
        }
        apply_outbound(connections, outbound);
        sweep(connections);

        pollfds.clear();
        pollfds.push_back(pollfd{listen.fd(), POLLIN, 0});
        pollfds.push_back(pollfd{wake.get(), POLLIN, 0});
        std::vector<std::size_t> polled;
        polled.reserve(connections.size());
        for (std::size_t i = 0; i < connections.size(); ++i) {
            const Connection& connection = connections[i];
            if (connection.closing || !connection.socket.valid()) {
                continue;
            }
            short events = 0;
            if (!connection.pending) {
                events = static_cast<short>(events | POLLIN);
            }
            if (connection.out_pos < connection.outbound.size()) {
                events = static_cast<short>(events | POLLOUT);
            }
            if (events == 0) {
                continue;
            }
            pollfds.push_back(pollfd{connection.socket.fd(), events, 0});
            polled.push_back(i);
        }

        const int ready =
            ::poll(pollfds.data(), static_cast<nfds_t>(pollfds.size()), kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll failed: " << std::strerror(errno) << '\n';
            break;
        }
        if (stop.stop_requested()) {
            break;
        }
        if (ready == 0) {
            continue;
        }
        if ((pollfds[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            drain_wake(wake.get());
        }
        if (stop.stop_requested()) {
            break;
        }
        if ((pollfds[0].revents & POLLIN) != 0) {
            accept_clients(listen, connections, next_session);
        }
        for (std::size_t n = 0; n < polled.size(); ++n) {
            const short revents = pollfds[n + 2].revents;
            if (revents == 0) {
                continue;
            }
            Connection& connection = connections[polled[n]];
            if ((revents & POLLIN) != 0) {
                read_socket(connection, inbound);
            }
            if ((revents & POLLOUT) != 0) {
                write_socket(connection);
            }
            if ((revents & (POLLERR | POLLNVAL)) != 0 ||
                ((revents & POLLHUP) != 0 && (revents & POLLIN) == 0)) {
                begin_close(connection);
            }
        }
    }
}

} // namespace antor::net
