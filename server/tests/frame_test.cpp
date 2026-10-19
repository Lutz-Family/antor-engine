#include "antor/net/frame.hpp"
#include "test_harness.hpp"

#include <vector>

void test_frame() {
    const std::byte payload[] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::vector<std::byte> frame;
    EXPECT(antor::net::write_frame(frame, payload));
    EXPECT(frame.size() == antor::net::kFrameHeaderBytes + sizeof(payload));

    antor::net::FrameView view;
    EXPECT(antor::net::inspect_frame(std::span<const std::byte>(frame.data(), 3), view) ==
           antor::net::FrameStatus::NeedMore);
    EXPECT(antor::net::inspect_frame(frame, view) == antor::net::FrameStatus::Ok);
    EXPECT(view.consumed == frame.size());
    EXPECT(view.payload.size() == sizeof(payload));
    EXPECT(view.payload[3] == std::byte{4});

    std::vector<std::byte> empty;
    EXPECT(!antor::net::write_frame(empty, {}));

    std::vector<std::byte> huge(antor::net::kMaxPayloadBytes + 1);
    std::vector<std::byte> rejected;
    EXPECT(!antor::net::write_frame(rejected, huge));

    std::byte header[4];
    antor::net::write_u32_le(header, static_cast<std::uint32_t>(antor::net::kMaxPayloadBytes + 1));
    EXPECT(antor::net::inspect_frame(header, view) == antor::net::FrameStatus::Error);

    antor::net::write_u32_le(header, 0);
    EXPECT(antor::net::inspect_frame(header, view) == antor::net::FrameStatus::Error);
}
