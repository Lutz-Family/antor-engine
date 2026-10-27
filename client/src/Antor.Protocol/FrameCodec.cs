using System.Buffers.Binary;

namespace Antor.Protocol;

/// <summary>
/// Length-prefixed frames: little-endian uint32 payload length, then the payload.
/// The same layout is implemented by <c>antor::transport::tcp::write_frame</c>.
/// </summary>
public static class FrameCodec
{
    public const int HeaderSize = 4;
    public const int MaxPayloadLength = 64 * 1024;

    public static bool TryWrite(Span<byte> destination, ReadOnlySpan<byte> payload, out int written)
    {
        written = 0;
        if (payload.Length == 0 || payload.Length > MaxPayloadLength)
        {
            return false;
        }

        var total = checked(HeaderSize + payload.Length);
        if (destination.Length < total)
        {
            return false;
        }

        BinaryPrimitives.WriteUInt32LittleEndian(destination, (uint)payload.Length);
        payload.CopyTo(destination.Slice(HeaderSize));
        written = total;
        return true;
    }
}
