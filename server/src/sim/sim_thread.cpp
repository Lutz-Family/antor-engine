#include "antor/sim/sim_thread.hpp"

#include "antor/protocol/codec.hpp"
#include "antor/protocol/version.hpp"
#include "antor/runtime/tick_clock.hpp"
#include "antor/zone/interest.hpp"
#include "antor/zone/zone.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace antor::sim {
namespace {

constexpr int kMaxInboundPerTick = 256;

struct Session {
    bool handshaken = false;
};

class SimLoop {
public:
    SimLoop(const ServerConfig& config, NetToSimQueue& inbound, SimToNetQueue& outbound)
        : inbound_(inbound), outbound_(outbound), zone_(1, config.shard_id), period_(config.tick),
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

    void enqueue(SimToNet message) {
        if (!pending_.empty() || !outbound_.try_push(std::move(message))) {
            pending_.push_back(std::move(message));
        }
        flush();
    }

    void flush() {
        while (!pending_.empty()) {
            if (!outbound_.try_push(std::move(pending_.front()))) {
                return;
            }
            pending_.pop_front();
        }
    }

    void drain() {
        flush();
        NetToSim message;
        int handled = 0;
        while (handled < kMaxInboundPerTick && inbound_.try_pop(message)) {
            handle(std::move(message));
            ++handled;
        }
        flush();
    }

    void handle(NetToSim message) {
        switch (message.type) {
        case NetToSim::Type::SessionOpened:
            sessions_[message.session] = Session{};
            zone_.add_session(message.session);
            break;
        case NetToSim::Type::SessionClosed:
            sessions_.erase(message.session);
            zone_.remove_session(message.session);
            break;
        case NetToSim::Type::Packet:
            handle_packet(message.session, message.bytes);
            break;
        }
    }

    void handle_packet(std::uint64_t session, const std::vector<std::byte>& bytes) {
        const auto found = sessions_.find(session);
        if (found == sessions_.end() || found->second.handshaken) {
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
        SimToNet reply;
        reply.type = SimToNet::Type::Send;
        reply.session = session;
        reply.bytes =
            codec_.hello_ack(protocol::kProtocolVersion, next_nonce_++, decoded.client_nonce);
        enqueue(std::move(reply));
    }

    void disconnect(std::uint64_t session) {
        sessions_.erase(session);
        zone_.remove_session(session);
        SimToNet message;
        message.type = SimToNet::Type::Disconnect;
        message.session = session;
        enqueue(std::move(message));
    }

    NetToSimQueue& inbound_;
    SimToNetQueue& outbound_;
    zone::Zone zone_;
    zone::InterestManager interest_;
    protocol::Codec codec_;
    std::unordered_map<std::uint64_t, Session> sessions_;
    std::deque<SimToNet> pending_;
    std::chrono::milliseconds period_;
    std::uint32_t shard_id_ = 0;
    std::uint32_t next_nonce_ = 1;
    std::uint64_t ticks_ = 0;
    std::uint64_t clamped_ = 0;
};

} // namespace

void run_sim(std::stop_token stop,
             const ServerConfig& config,
             NetToSimQueue& inbound,
             SimToNetQueue& outbound) {
    SimLoop loop(config, inbound, outbound);
    loop.run(std::move(stop));
}

} // namespace antor::sim
