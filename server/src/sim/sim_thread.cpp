#include "antor/sim/sim_thread.hpp"

#include "antor/protocol/codec.hpp"
#include "antor/protocol/version.hpp"
#include "antor/runtime/tick_clock.hpp"
#include "antor/transport/session_bus.hpp"
#include "antor/zone/interest.hpp"
#include "antor/zone/zone.hpp"

#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace antor::sim {
namespace {

constexpr int kMaxInboundPerTick = 256;

struct Session {
    bool handshaken = false;
};

class SimLoop {
public:
    SimLoop(const ServerConfig& config,
            transport::InboundQueue& inbound,
            transport::OutboundQueue& outbound)
        : bus_(inbound, outbound), zone_(1, config.shard_id), period_(config.tick),
          shard_id_(config.shard_id) {}

    void run(std::stop_token stop) {
        TickClock<> clock(period_);
        clock.start(Clock::now());
        std::mutex wait_mutex;
        std::condition_variable wake;
        // Hold the wait mutex across the notify so a stop cannot be lost
        // between the predicate check and the wait.
        const std::stop_callback callback(stop, [&wait_mutex, &wake] {
            std::lock_guard guard(wait_mutex);
            wake.notify_all();
        });

        while (!stop.stop_requested()) {
            drain();
            zone_.tick(period_);
            // No replicated entities yet. The call keeps the interest seam on
            // the sim thread, which is the only thread allowed to read the zone.
            const auto audience = interest_.recipients(zone_);
            static_cast<void>(audience);
            ++ticks_;

            bool clamped = false;
            const auto sleep_for = clock.complete_tick(Clock::now(), clamped);
            if (clamped) {
                ++clamped_;
            }
            if (stop.stop_requested()) {
                break;
            }
            std::unique_lock lock(wait_mutex);
            wake.wait_for(lock, sleep_for, [&stop] { return stop.stop_requested(); });
        }

        std::cerr << "sim stopped shard=" << shard_id_ << " ticks=" << ticks_
                  << " clamped=" << clamped_ << '\n';
    }

private:
    using Clock = std::chrono::steady_clock;

    void drain() {
        bus_.flush();
        transport::InboundEvent message;
        int handled = 0;
        while (handled < kMaxInboundPerTick && bus_.try_pop(message)) {
            handle(std::move(message));
            ++handled;
        }
        bus_.flush();
    }

    void handle(transport::InboundEvent message) {
        switch (message.type) {
        case transport::InboundEvent::Type::SessionOpened:
            sessions_[message.session] = Session{};
            zone_.add_session(message.session);
            break;
        case transport::InboundEvent::Type::SessionClosed:
            sessions_.erase(message.session);
            zone_.remove_session(message.session);
            break;
        case transport::InboundEvent::Type::Packet:
            handle_packet(message.session, message.channel, message.bytes);
            break;
        }
    }

    void handle_packet(std::uint64_t session,
                       transport::Channel channel,
                       const std::vector<std::byte>& bytes) {
        const auto found = sessions_.find(session);
        // The handshake is reliable-ordered. A different delivery is refused
        // here so a future datagram worker cannot deliver Hello on a channel
        // this code did not agree to.
        if (found == sessions_.end() || found->second.handshaken ||
            channel.delivery != transport::Delivery::ReliableOrdered) {
            disconnect(session);
            return;
        }
        const auto decoded = codec_.decode(bytes);
        if (decoded.kind != protocol::PacketKind::Hello ||
            decoded.protocol_version != protocol::kProtocolVersion) {
            disconnect(session);
            return;
        }
        found->second.handshaken = true;
        bus_.send(
            session,
            transport::Channel::reliable(),
            codec_.hello_ack(protocol::kProtocolVersion, next_nonce_++, decoded.client_nonce));
    }

    void disconnect(std::uint64_t session) {
        sessions_.erase(session);
        zone_.remove_session(session);
        bus_.disconnect(session);
    }

    transport::SessionBus bus_;
    zone::Zone zone_;
    zone::InterestManager interest_;
    protocol::Codec codec_;
    std::unordered_map<std::uint64_t, Session> sessions_;
    std::chrono::milliseconds period_;
    std::uint32_t shard_id_ = 0;
    std::uint32_t next_nonce_ = 1;
    std::uint64_t ticks_ = 0;
    std::uint64_t clamped_ = 0;
};

} // namespace

void run_sim(std::stop_token stop,
             const ServerConfig& config,
             transport::InboundQueue& inbound,
             transport::OutboundQueue& outbound) {
    SimLoop loop(config, inbound, outbound);
    loop.run(std::move(stop));
}

} // namespace antor::sim
