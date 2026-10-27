#include "antor/protocol/codec.hpp"

#include "antor/protocol/version.hpp"
#include "antor_generated.h"

#include <cstring>
#include <utility>

namespace antor::protocol {
namespace {

std::vector<std::byte> snapshot(flatbuffers::FlatBufferBuilder& builder) {
    const auto size = builder.GetSize();
    std::vector<std::byte> out(size);
    if (size != 0) {
        std::memcpy(out.data(), builder.GetBufferPointer(), size);
    }
    return out;
}

} // namespace

struct Codec::Impl {
    flatbuffers::FlatBufferBuilder builder{256};

    Impl() { builder.ForceDefaults(true); }

    std::vector<std::byte> finish_hello(std::uint16_t protocol_version,
                                        std::uint32_t client_nonce) {
        builder.Clear();
        const auto hello = Antor::Protocol::CreateHello(builder, protocol_version, client_nonce);
        const auto packet =
            Antor::Protocol::CreatePacket(builder, Antor::Protocol::Body::Hello, hello.Union());
        Antor::Protocol::FinishPacketBuffer(builder, packet);
        return snapshot(builder);
    }

    std::vector<std::byte> finish_hello_ack(std::uint16_t protocol_version,
                                            std::uint32_t server_nonce,
                                            std::uint32_t client_nonce) {
        builder.Clear();
        const auto ack =
            Antor::Protocol::CreateHelloAck(builder, protocol_version, server_nonce, client_nonce);
        const auto packet =
            Antor::Protocol::CreatePacket(builder, Antor::Protocol::Body::HelloAck, ack.Union());
        Antor::Protocol::FinishPacketBuffer(builder, packet);
        return snapshot(builder);
    }
};

Codec::Codec() : impl_(std::make_unique<Impl>()) {}
Codec::~Codec() = default;
Codec::Codec(Codec&&) noexcept = default;
Codec& Codec::operator=(Codec&&) noexcept = default;

std::vector<std::byte> Codec::hello(std::uint16_t protocol_version, std::uint32_t client_nonce) {
    return impl_->finish_hello(protocol_version, client_nonce);
}

std::vector<std::byte> Codec::hello_ack(std::uint16_t protocol_version,
                                        std::uint32_t server_nonce,
                                        std::uint32_t client_nonce) {
    return impl_->finish_hello_ack(protocol_version, server_nonce, client_nonce);
}

DecodedPacket Codec::decode(std::span<const std::byte> payload) {
    DecodedPacket decoded;
    if (payload.empty()) {
        return decoded;
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(data, payload.size());
    if (!Antor::Protocol::VerifyPacketBuffer(verifier)) {
        return decoded;
    }
    const auto* packet = Antor::Protocol::GetPacket(data);
    if (packet == nullptr) {
        return decoded;
    }
    switch (packet->body_type()) {
    case Antor::Protocol::Body::Hello: {
        const auto* hello = packet->body_as_Hello();
        if (hello == nullptr) {
            return decoded;
        }
        decoded.kind = PacketKind::Hello;
        decoded.protocol_version = hello->protocol_version();
        decoded.client_nonce = hello->client_nonce();
        return decoded;
    }
    case Antor::Protocol::Body::HelloAck: {
        const auto* ack = packet->body_as_HelloAck();
        if (ack == nullptr) {
            return decoded;
        }
        decoded.kind = PacketKind::HelloAck;
        decoded.protocol_version = ack->protocol_version();
        decoded.server_nonce = ack->server_nonce();
        decoded.client_nonce = ack->client_nonce();
        return decoded;
    }
    case Antor::Protocol::Body::NONE:
        return decoded;
    }
    return decoded;
}

} // namespace antor::protocol
