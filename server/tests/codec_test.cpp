#include "antor/protocol/codec.hpp"
#include "antor/protocol/version.hpp"
#include "test_harness.hpp"

void test_codec() {
    antor::protocol::Codec codec;
    const auto hello = codec.hello(antor::protocol::kProtocolVersion, 0xA11CEu);
    const auto decoded_hello = codec.decode(hello);
    EXPECT(decoded_hello.kind == antor::protocol::PacketKind::Hello);
    EXPECT(decoded_hello.protocol_version == antor::protocol::kProtocolVersion);
    EXPECT(decoded_hello.client_nonce == 0xA11CEu);

    const auto ack = codec.hello_ack(antor::protocol::kProtocolVersion, 9, 0xA11CEu);
    const auto decoded_ack = codec.decode(ack);
    EXPECT(decoded_ack.kind == antor::protocol::PacketKind::HelloAck);
    EXPECT(decoded_ack.server_nonce == 9);
    EXPECT(decoded_ack.client_nonce == 0xA11CEu);

    const auto again = codec.hello(antor::protocol::kProtocolVersion, 1);
    EXPECT(codec.decode(again).client_nonce == 1);

    const std::byte garbage[] = {std::byte{1},
                                 std::byte{2},
                                 std::byte{3},
                                 std::byte{4},
                                 std::byte{5},
                                 std::byte{6},
                                 std::byte{7},
                                 std::byte{8}};
    EXPECT(codec.decode(garbage).kind == antor::protocol::PacketKind::Invalid);
    EXPECT(codec.decode({}).kind == antor::protocol::PacketKind::Invalid);
}
