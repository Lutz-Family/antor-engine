using Antor.Protocol;
using Xunit;

namespace Antor.Protocol.Tests;

public class ProtocolTests
{
    [Fact]
    public void Frame_RoundTrip_AndRejectsBadLengths()
    {
        var payload = new byte[] { 1, 2, 3, 4 };
        var frame = new byte[32];
        Assert.True(FrameCodec.TryWrite(frame, payload, out var written));
        Assert.Equal(FrameCodec.HeaderSize + payload.Length, written);

        var storage = new byte[FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength];
        var assembler = new FrameAssembler(storage);
        frame.AsSpan(0, 3).CopyTo(assembler.WritableRegion().Span);
        assembler.Commit(3);
        Assert.Equal(FrameRead.NeedMore, assembler.TryRead(out _, out _));

        frame.AsSpan(3, written - 3).CopyTo(assembler.WritableRegion().Span);
        assembler.Commit(written - 3);
        Assert.Equal(FrameRead.Frame, assembler.TryRead(out var offset, out var length));
        Assert.Equal(payload, storage.AsSpan(offset, length).ToArray());

        var bad = new byte[FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength];
        var rejected = new FrameAssembler(bad);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(
            rejected.WritableRegion().Span,
            FrameCodec.MaxPayloadLength + 1);
        rejected.Commit(FrameCodec.HeaderSize);
        Assert.Equal(FrameRead.Error, rejected.TryRead(out _, out _));
    }

    [Fact]
    public void Assembler_FeedsOneByteAtATime()
    {
        using var codec = new PacketCodec();
        var payload = new byte[256];
        var payloadLength = codec.WriteHello(payload, ProtocolVersion.Current, 0xABCDu);
        Assert.True(payloadLength > 0);
        var frame = new byte[payloadLength + FrameCodec.HeaderSize];
        Assert.True(FrameCodec.TryWrite(frame, payload.AsSpan(0, payloadLength), out var written));

        var storage = new byte[FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength];
        var assembler = new FrameAssembler(storage);
        FrameRead status = FrameRead.NeedMore;
        var offset = 0;
        var length = 0;
        for (var i = 0; i < written; i++)
        {
            var region = assembler.WritableRegion();
            region.Span[0] = frame[i];
            assembler.Commit(1);
            status = assembler.TryRead(out offset, out length);
            if (i + 1 < written)
            {
                Assert.Equal(FrameRead.NeedMore, status);
            }
        }

        Assert.Equal(FrameRead.Frame, status);
        Assert.True(codec.TryDecode(storage.AsSpan(offset, length), out var decoded));
        Assert.Equal(PacketKind.Hello, decoded.Kind);
        Assert.Equal(0xABCDu, decoded.ClientNonce);
    }

    [Fact]
    public void Codec_RoundTrip_DoesNotAllocateAfterWarmup()
    {
        using var codec = new PacketCodec();
        var buffer = new byte[256];
        for (var i = 0; i < 32; i++)
        {
            var warmHello = codec.WriteHello(buffer, ProtocolVersion.Current, 7);
            Assert.True(codec.TryDecode(buffer.AsSpan(0, warmHello), out _));
            var warmAck = codec.WriteHelloAck(buffer, ProtocolVersion.Current, 8, 7);
            Assert.True(codec.TryDecode(buffer.AsSpan(0, warmAck), out _));
        }

        var before = GC.GetAllocatedBytesForCurrentThread();
        uint nonce = 0;
        for (var i = 0; i < 1000; i++)
        {
            var helloBytes = codec.WriteHello(buffer, ProtocolVersion.Current, 0xA11CEu);
            if (!codec.TryDecode(buffer.AsSpan(0, helloBytes), out var hello))
            {
                throw new InvalidOperationException("Hello did not decode.");
            }

            var ackBytes = codec.WriteHelloAck(buffer, ProtocolVersion.Current, 3, hello.ClientNonce);
            if (!codec.TryDecode(buffer.AsSpan(0, ackBytes), out var ack))
            {
                throw new InvalidOperationException("HelloAck did not decode.");
            }

            nonce = ack.ClientNonce;
        }

        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Assert.Equal(0xA11CEu, nonce);
        Assert.Equal(0L, allocated);
    }
}
