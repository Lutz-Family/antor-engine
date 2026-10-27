#include "antor/transport/tcp/frame.hpp"
#include "antor/transport/tcp/poll_transport.hpp"
#include "test_harness.hpp"

#include <vector>

void test_frame() {
    const std::byte payload[] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::vector<std::byte> frame;
    EXPECT(antor::transport::tcp::write_frame(frame, payload));
    EXPECT(frame.size() == antor::transport::tcp::kFrameHeaderBytes + sizeof(payload));

    antor::transport::tcp::FrameView view;
    EXPECT(
        antor::transport::tcp::inspect_frame(std::span<const std::byte>(frame.data(), 3), view) ==
        antor::transport::tcp::FrameStatus::NeedMore);
    EXPECT(antor::transport::tcp::inspect_frame(frame, view) ==
           antor::transport::tcp::FrameStatus::Ok);
    EXPECT(view.consumed == frame.size());
    EXPECT(view.payload.size() == sizeof(payload));
    EXPECT(view.payload[3] == std::byte{4});

    std::vector<std::byte> empty;
    EXPECT(!antor::transport::tcp::write_frame(empty, {}));

    std::vector<std::byte> huge(antor::transport::tcp::kMaxPayloadBytes + 1);
    std::vector<std::byte> rejected;
    EXPECT(!antor::transport::tcp::write_frame(rejected, huge));

    std::byte header[4];
    antor::transport::tcp::write_u32_le(
        header, static_cast<std::uint32_t>(antor::transport::tcp::kMaxPayloadBytes + 1));
    EXPECT(antor::transport::tcp::inspect_frame(header, view) ==
           antor::transport::tcp::FrameStatus::Error);

    antor::transport::tcp::write_u32_le(header, 0);
    EXPECT(antor::transport::tcp::inspect_frame(header, view) ==
           antor::transport::tcp::FrameStatus::Error);

    EXPECT(antor::transport::tcp::is_reliable_ordered(antor::transport::Channel::reliable()));
    EXPECT(!antor::transport::tcp::is_reliable_ordered(antor::transport::Channel::sequenced(1)));
}
