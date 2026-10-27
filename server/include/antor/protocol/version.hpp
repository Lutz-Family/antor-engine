#pragma once

#include <cstdint>

namespace antor::protocol {

// Keep in lockstep with the Hello.protocol_version default in protocol/antor.fbs
// and with Antor.Protocol.ProtocolVersion.Current.
inline constexpr std::uint16_t kProtocolVersion = 1;

inline constexpr char kFileIdentifier[] = "ANTR";

} // namespace antor::protocol
