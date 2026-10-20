using System.Buffers;
using Antor.Protocol;

namespace Antor.Net;

/// <summary>
/// Length-prefixed <see cref="ITransport"/> over an <see cref="IBytePipe"/>.
/// Implements <see cref="Delivery.ReliableOrdered"/> only. A datagram transport
/// implements <see cref="ITransport"/> directly and does not use this type.
/// Send copies into a rented frame buffer and returns the pipe's task, so this
/// layer does not allocate an async state machine of its own.
/// </summary>
public sealed class FramedTransport : ITransport
{
    private readonly IBytePipe _pipe;
    private readonly byte[] _recv;
    private readonly byte[] _send;
    private readonly FrameAssembler _frames;
    private bool _disposed;

    public FramedTransport(IBytePipe pipe)
    {
        ArgumentNullException.ThrowIfNull(pipe);
        var recv = ArrayPool<byte>.Shared.Rent(FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength);
        byte[]? send = null;
        try
        {
            send = ArrayPool<byte>.Shared.Rent(FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength);
            _frames = new FrameAssembler(recv);
        }
        catch
        {
            ArrayPool<byte>.Shared.Return(recv);
            if (send is not null)
            {
                ArrayPool<byte>.Shared.Return(send);
            }

            throw;
        }

        _recv = recv;
        _send = send!;
        _pipe = pipe;
    }

    public ValueTask SendAsync(
        Channel channel,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken = default)
    {
        if (channel.Delivery != Delivery.ReliableOrdered)
        {
            throw new InvalidOperationException(
                "This transport implements reliable-ordered delivery only.");
        }

        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!FrameCodec.TryWrite(_send, payload.Span, out var written))
        {
            throw new ProtocolException("Payload does not fit in one frame.");
        }

        return _pipe.SendAsync(_send.AsMemory(0, written), cancellationToken);
    }

    public async ValueTask<TransportPacket> ReceiveAsync(CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        while (true)
        {
            var status = _frames.TryRead(out var offset, out var length);
            if (status == FrameRead.Error)
            {
                throw new ProtocolException("Peer sent a malformed frame.");
            }

            if (status == FrameRead.Frame)
            {
                return new TransportPacket(Channel.Reliable, _recv.AsMemory(offset, length));
            }

            var region = _frames.WritableRegion();
            if (region.IsEmpty)
            {
                throw new ProtocolException("Receive buffer is full.");
            }

            var read = await _pipe.ReceiveAsync(region, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                throw new ProtocolException("Peer closed the connection.");
            }

            _frames.Commit(read);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _pipe.Dispose();
        ArrayPool<byte>.Shared.Return(_recv);
        ArrayPool<byte>.Shared.Return(_send);
    }
}
