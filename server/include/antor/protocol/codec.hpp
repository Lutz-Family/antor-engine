#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace antor::protocol {

enum class PacketKind : std::uint8_t { Invalid, Hello, HelloAck };

struct DecodedPacket {
    PacketKind kind = PacketKind::Invalid;
    std::uint16_t protocol_version = 0;
    std::uint32_t server_nonce = 0;
    std::uint32_t client_nonce = 0;
};

// FlatBuffers codec owned by one thread. The builder is reused across calls.
// Payloads are unframed. The transport adds whatever envelope its channel uses.
class Codec {
public:
    Codec();
    ~Codec();

    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    Codec(Codec&&) noexcept;
    Codec& operator=(Codec&&) noexcept;

    [[nodiscard]] std::vector<std::byte> hello(std::uint16_t protocol_version,
                                               std::uint32_t client_nonce);
    [[nodiscard]] std::vector<std::byte> hello_ack(std::uint16_t protocol_version,
                                                   std::uint32_t server_nonce,
                                                   std::uint32_t client_nonce);
    [[nodiscard]] DecodedPacket decode(std::span<const std::byte> payload);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace antor::protocol
