using System.Buffers;
using Google.FlatBuffers;

namespace Antor.Protocol;

public enum PacketKind : byte
{
    Invalid,
    Hello,
    HelloAck,
}

public readonly struct DecodedPacket
{
    public DecodedPacket(PacketKind kind, ushort protocolVersion, uint serverNonce, uint clientNonce)
    {
        Kind = kind;
        ProtocolVersion = protocolVersion;
        ServerNonce = serverNonce;
        ClientNonce = clientNonce;
    }

    public PacketKind Kind { get; }
    public ushort ProtocolVersion { get; }
    public uint ServerNonce { get; }
    public uint ClientNonce { get; }
}

/// <summary>
/// Builds and reads handshake packets without allocating on the steady-state path.
/// One instance is owned by one caller; it is not thread-safe.
/// The FlatBuffer builder, verifier, and decode scratch buffer are reused.
/// </summary>
public sealed class PacketCodec : IDisposable
{
    private static readonly VerifyTableAction VerifyPacketAction = PacketVerify.Verify;

    private readonly FlatBufferBuilder _builder = new(256) { ForceDefaults = true };
    private readonly byte[] _scratch = ArrayPool<byte>.Shared.Rent(FrameCodec.MaxPayloadLength);
    private readonly BoundedByteBufferAllocator _allocator = new();
    private readonly ByteBuffer _buffer;
    private readonly Verifier _verifier;
    private bool _disposed;

    public PacketCodec()
    {
        _allocator.Assign(_scratch, 0);
        _buffer = new ByteBuffer(_allocator, 0);
        _verifier = new Verifier(_buffer);
    }

    public int WriteHello(Span<byte> destination, ushort protocolVersion, uint clientNonce)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _builder.Clear();
        var hello = Hello.CreateHello(_builder, protocolVersion, clientNonce);
        var packet = Packet.CreatePacket(_builder, Body.Hello, hello.Value);
        Packet.FinishPacketBuffer(_builder, packet);
        return WriteFinished(destination);
    }

    public int WriteHelloAck(
        Span<byte> destination,
        ushort protocolVersion,
        uint serverNonce,
        uint clientNonce)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _builder.Clear();
        var ack = HelloAck.CreateHelloAck(_builder, protocolVersion, serverNonce, clientNonce);
        var packet = Packet.CreatePacket(_builder, Body.HelloAck, ack.Value);
        Packet.FinishPacketBuffer(_builder, packet);
        return WriteFinished(destination);
    }

    public bool TryDecode(ReadOnlySpan<byte> payload, out DecodedPacket decoded)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        decoded = default;
        if (payload.Length == 0 || payload.Length > _scratch.Length)
        {
            return false;
        }

        payload.CopyTo(_scratch);
        _allocator.Assign(_scratch, payload.Length);
        _buffer.Position = 0;
        _verifier.Buf = _buffer;
        if (!_verifier.VerifyBuffer(ProtocolVersion.FileIdentifier, false, VerifyPacketAction))
        {
            return false;
        }

        var packet = Packet.GetRootAsPacket(_buffer);
        switch (packet.BodyType)
        {
            case Body.Hello:
            {
                var hello = packet.BodyAsHello();
                decoded = new DecodedPacket(
                    PacketKind.Hello,
                    hello.ProtocolVersion,
                    0,
                    hello.ClientNonce);
                return true;
            }
            case Body.HelloAck:
            {
                var ack = packet.BodyAsHelloAck();
                decoded = new DecodedPacket(
                    PacketKind.HelloAck,
                    ack.ProtocolVersion,
                    ack.ServerNonce,
                    ack.ClientNonce);
                return true;
            }
            default:
                return false;
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        ArrayPool<byte>.Shared.Return(_scratch);
    }

    private int WriteFinished(Span<byte> destination)
    {
        var data = _builder.DataBuffer;
        var segment = data.ToArraySegment(data.Position, _builder.Offset);
        return FrameCodec.TryWrite(destination, segment.AsSpan(), out var written) ? written : 0;
    }
}
