using Antor.Protocol;
using Xunit;
using PipeChannel = System.Threading.Channels.Channel;

namespace Antor.Net.Tests;

public class FramedTransportTests
{
    [Fact]
    public async Task FramedTransport_RoundTripsPayload_WithoutASocket()
    {
        var (leftPipe, rightPipe) = ChunkPipe.CreatePair();
        using var left = new FramedTransport(leftPipe);
        using var right = new FramedTransport(rightPipe);
        var payload = new byte[] { 1, 2, 3, 4, 5 };

        await left.SendAsync(Channel.Reliable, payload).ConfigureAwait(true);
        var received = await right.ReceiveAsync().ConfigureAwait(true);

        Assert.Equal(Delivery.ReliableOrdered, received.Channel.Delivery);
        Assert.Equal(payload, received.Payload.ToArray());
    }

    [Fact]
    public void FramedTransport_RejectsUnreliable_BeforeTouchingThePipe()
    {
        using var transport = new FramedTransport(new BrokenPipe());
        var error = Assert.Throws<InvalidOperationException>(
            () => transport.SendAsync(Channel.Sequenced(3), new byte[] { 1 }));
        Assert.Contains("reliable-ordered", error.Message, StringComparison.Ordinal);
    }

    private sealed class BrokenPipe : IBytePipe
    {
        public ValueTask SendAsync(Memory<byte> bytes, CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("The byte pipe was used.");

        public ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("The byte pipe was used.");

        public void Dispose()
        {
        }
    }

    private sealed class ChunkPipe : IBytePipe
    {
        private readonly System.Threading.Channels.ChannelReader<byte[]> _reader;
        private readonly System.Threading.Channels.ChannelWriter<byte[]> _writer;
        private byte[]? _chunk;
        private int _offset;

        private ChunkPipe(
            System.Threading.Channels.ChannelReader<byte[]> reader,
            System.Threading.Channels.ChannelWriter<byte[]> writer)
        {
            _reader = reader;
            _writer = writer;
        }

        public static (ChunkPipe Left, ChunkPipe Right) CreatePair()
        {
            var leftToRight = PipeChannel.CreateUnbounded<byte[]>();
            var rightToLeft = PipeChannel.CreateUnbounded<byte[]>();
            return (
                new ChunkPipe(rightToLeft.Reader, leftToRight.Writer),
                new ChunkPipe(leftToRight.Reader, rightToLeft.Writer));
        }

        public ValueTask SendAsync(Memory<byte> bytes, CancellationToken cancellationToken = default) =>
            _writer.WriteAsync(bytes.ToArray(), cancellationToken);

        public async ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            if (_chunk is null || _offset >= _chunk.Length)
            {
                _chunk = await _reader.ReadAsync(cancellationToken).ConfigureAwait(false);
                _offset = 0;
            }

            var count = Math.Min(buffer.Length, _chunk.Length - _offset);
            _chunk.AsSpan(_offset, count).CopyTo(buffer.Span);
            _offset += count;
            return count;
        }

        public void Dispose() => _writer.TryComplete();
    }
}
