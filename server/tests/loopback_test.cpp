#include "antor/protocol/codec.hpp"
#include "antor/protocol/version.hpp"
#include "antor/sim/sim_thread.hpp"
#include "antor/transport/loopback.hpp"
#include "antor/transport/session_bus.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <thread>
#include <vector>

void test_session_bus_overflow() {
    antor::transport::InboundQueue inbound;
    antor::transport::OutboundQueue outbound;
    antor::transport::SessionBus bus(inbound, outbound);

    const auto slots = antor::transport::OutboundQueue::capacity();
    for (std::size_t i = 0; i < slots; ++i) {
        bus.send(7, antor::transport::Channel::reliable(), std::vector<std::byte>{std::byte{1}});
    }
    bus.send(7, antor::transport::Channel::reliable(), std::vector<std::byte>{std::byte{2}});

    std::size_t ones = 0;
    antor::transport::OutboundCommand command;
    while (outbound.try_pop(command)) {
        EXPECT(command.type == antor::transport::OutboundCommand::Type::Send);
        EXPECT(command.session == 7);
        EXPECT(command.channel == antor::transport::Channel::reliable());
        EXPECT(command.bytes.size() == 1);
        EXPECT(command.bytes[0] == std::byte{1});
        ++ones;
    }
    EXPECT(ones == slots);

    bus.flush();
    EXPECT(outbound.try_pop(command));
    EXPECT(command.bytes.size() == 1);
    EXPECT(command.bytes[0] == std::byte{2});
    EXPECT(!outbound.try_pop(command));
}

void test_loopback_handshake() {
    antor::ServerConfig config;
    config.tick = std::chrono::milliseconds(20);
    config.shard_id = 9;

    antor::transport::InboundQueue inbound;
    antor::transport::OutboundQueue outbound;
    antor::transport::loopback::Endpoint endpoint(inbound, outbound);
    std::jthread sim([&config, &inbound, &outbound](std::stop_token stop) {
        antor::sim::run_sim(stop, config, inbound, outbound);
    });

    antor::protocol::Codec codec;
    const auto first = endpoint.open();
    endpoint.deliver(first,
                     antor::transport::Channel::reliable(),
                     codec.hello(antor::protocol::kProtocolVersion, 0x1111u));

    antor::transport::OutboundCommand reply;
    EXPECT(endpoint.poll(reply, std::chrono::milliseconds(2000)));
    EXPECT(reply.type == antor::transport::OutboundCommand::Type::Send);
    EXPECT(reply.session == first);
    EXPECT(reply.channel == antor::transport::Channel::reliable());
    const auto first_ack = codec.decode(reply.bytes);
    EXPECT(first_ack.kind == antor::protocol::PacketKind::HelloAck);
    EXPECT(first_ack.protocol_version == antor::protocol::kProtocolVersion);
    EXPECT(first_ack.client_nonce == 0x1111u);
    EXPECT(first_ack.server_nonce != 0);

    const auto second = endpoint.open();
    endpoint.deliver(second,
                     antor::transport::Channel::reliable(),
                     codec.hello(antor::protocol::kProtocolVersion, 0x2222u));
    EXPECT(endpoint.poll(reply, std::chrono::milliseconds(2000)));
    const auto second_ack = codec.decode(reply.bytes);
    EXPECT(second_ack.kind == antor::protocol::PacketKind::HelloAck);
    EXPECT(second_ack.client_nonce == 0x2222u);
    EXPECT(second_ack.server_nonce != first_ack.server_nonce);

    const auto mismatched = endpoint.open();
    endpoint.deliver(
        mismatched,
        antor::transport::Channel::reliable(),
        codec.hello(static_cast<std::uint16_t>(antor::protocol::kProtocolVersion + 1), 0x3333u));
    EXPECT(endpoint.poll(reply, std::chrono::milliseconds(2000)));
    EXPECT(reply.type == antor::transport::OutboundCommand::Type::Disconnect);
    EXPECT(reply.session == mismatched);

    const auto unreliable = endpoint.open();
    endpoint.deliver(unreliable,
                     antor::transport::Channel::sequenced(1),
                     codec.hello(antor::protocol::kProtocolVersion, 0x4444u));
    EXPECT(endpoint.poll(reply, std::chrono::milliseconds(2000)));
    EXPECT(reply.type == antor::transport::OutboundCommand::Type::Disconnect);
    EXPECT(reply.session == unreliable);

    endpoint.close(first);
    sim.request_stop();
    sim.join();
}
