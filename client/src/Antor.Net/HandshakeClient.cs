using System.Buffers;
using Antor.Protocol;

namespace Antor.Net;

public readonly struct HandshakeResult
{
    public HandshakeResult(ushort protocolVersion, uint serverNonce, uint clientNonce)
    {
        ProtocolVersion = protocolVersion;
        ServerNonce = serverNonce;
        ClientNonce = clientNonce;
    }

    public ushort ProtocolVersion { get; }

    public uint ServerNonce { get; }

    public uint ClientNonce { get; }
}

/// <summary>
/// Handshake against any <see cref="ITransport"/>. The codec writes an unframed
/// payload; the transport decides how those bytes move.
/// </summary>
public static class HandshakeClient
{
    public static async ValueTask<HandshakeResult> ExchangeAsync(
        ITransport transport,
        uint clientNonce,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(transport);
        using var codec = new PacketCodec();
        var buffer = ArrayPool<byte>.Shared.Rent(FrameCodec.MaxPayloadLength);
        try
        {
            var written = codec.WriteHello(buffer, ProtocolVersion.Current, clientNonce);
            if (written <= 0)
            {
                throw new ProtocolException("Failed to encode Hello.");
            }

            await transport.SendAsync(Channel.Reliable, buffer.AsMemory(0, written), cancellationToken)
                .ConfigureAwait(false);
            var incoming = await transport.ReceiveAsync(cancellationToken).ConfigureAwait(false);
            if (incoming.Channel.Delivery != Delivery.ReliableOrdered ||
                !codec.TryDecode(incoming.Payload.Span, out var decoded) ||
                decoded.Kind != PacketKind.HelloAck ||
                decoded.ProtocolVersion != ProtocolVersion.Current ||
                decoded.ClientNonce != clientNonce)
            {
                throw new ProtocolException("Server rejected the handshake.");
            }

            return new HandshakeResult(decoded.ProtocolVersion, decoded.ServerNonce, decoded.ClientNonce);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }
}
