namespace Antor.Net;

/// <summary>
/// One payload delivered by <see cref="ITransport.ReceiveAsync"/>.
/// <see cref="Payload"/> stays valid until the next receive or dispose on that transport.
/// </summary>
public readonly struct TransportPacket
{
    public TransportPacket(Channel channel, ReadOnlyMemory<byte> payload)
    {
        Channel = channel;
        Payload = payload;
    }

    public Channel Channel { get; }

    public ReadOnlyMemory<byte> Payload { get; }
}

/// <summary>
/// Session boundary on the client. Payloads are unframed FlatBuffers bytes.
/// Implementations in this tree are used by one caller, with one send and one
/// receive in flight. The call is virtual once per message; byte pumps inside
/// an implementation are not.
/// </summary>
public interface ITransport : IDisposable
{
    /// <summary>
    /// <paramref name="payload"/> must stay unchanged until the returned task completes.
    /// </summary>
    ValueTask SendAsync(Channel channel, ReadOnlyMemory<byte> payload, CancellationToken cancellationToken = default);

    ValueTask<TransportPacket> ReceiveAsync(CancellationToken cancellationToken = default);
}
