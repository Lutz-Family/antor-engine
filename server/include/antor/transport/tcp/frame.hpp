#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace antor::transport::tcp {

static_assert(std::endian::native == std::endian::little,
              "v0 framing writes native uint32 as little-endian");

inline constexpr std::size_t kFrameHeaderBytes = 4;
inline constexpr std::size_t kMaxPayloadBytes = 64 * 1024;

enum class FrameStatus { NeedMore, Ok, Error };

struct FrameView {
    std::span<const std::byte> payload;
    std::size_t consumed = 0;
};

inline std::uint32_t read_u32_le(const std::byte* data) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

inline void write_u32_le(std::byte* data, std::uint32_t value) noexcept {
    std::memcpy(data, &value, sizeof(value));
}

// A frame is a little-endian uint32 payload length followed by that many
// bytes. Length 0 and lengths above kMaxPayloadBytes are errors. The payload
// span points into `buffer` and is valid only as long as that memory is.
inline FrameStatus inspect_frame(std::span<const std::byte> buffer, FrameView& out) noexcept {
    out = {};
    if (buffer.size() < kFrameHeaderBytes) {
        return FrameStatus::NeedMore;
    }
    const auto length = read_u32_le(buffer.data());
    if (length == 0 || length > kMaxPayloadBytes) {
        return FrameStatus::Error;
    }
    const auto total = kFrameHeaderBytes + static_cast<std::size_t>(length);
    if (buffer.size() < total) {
        return FrameStatus::NeedMore;
    }
    out.payload = buffer.subspan(kFrameHeaderBytes, length);
    out.consumed = total;
    return FrameStatus::Ok;
}

inline bool write_frame(std::vector<std::byte>& out, std::span<const std::byte> payload) {
    if (payload.empty() || payload.size() > kMaxPayloadBytes) {
        return false;
    }
    const auto length = static_cast<std::uint32_t>(payload.size());
    const auto offset = out.size();
    out.resize(offset + kFrameHeaderBytes + payload.size());
    write_u32_le(out.data() + offset, length);
    std::memcpy(out.data() + offset + kFrameHeaderBytes, payload.data(), payload.size());
    return true;
}

} // namespace antor::transport::tcp
