using Antor.Protocol;
using Xunit;
using PipeChannel = System.Threading.Channels.Channel;

namespace Antor.Net.Tests;

public class LoopbackHandshakeTests
{
    [Fact]
    public async Task Handshake_Completes_OverInMemoryTransport()
    {
        var (client, peer) = LoopbackTransport.CreatePair();
        using (client)
        using (peer)
        {
            var exchange = HandshakeClient.ExchangeAsync(client, 0xC0FFEEu).AsTask();
            var helloPacket = await peer.ReceiveAsync().ConfigureAwait(true);
            Assert.Equal(Delivery.ReliableOrdered, helloPacket.Channel.Delivery);
            Assert.Equal(0u, helloPacket.Channel.Sequence);

            using var codec = new PacketCodec();
            Assert.True(codec.TryDecode(helloPacket.Payload.Span, out var hello));
            Assert.Equal(PacketKind.Hello, hello.Kind);
            Assert.Equal(0xC0FFEEu, hello.ClientNonce);

            var ack = new byte[256];
            var written = codec.WriteHelloAck(ack, ProtocolVersion.Current, 99, hello.ClientNonce);
            Assert.True(written > 0);
            await peer.SendAsync(Channel.Reliable, ack.AsMemory(0, written)).ConfigureAwait(true);

            var result = await exchange.ConfigureAwait(true);
            Assert.Equal(ProtocolVersion.Current, result.ProtocolVersion);
            Assert.Equal(0xC0FFEEu, result.ClientNonce);
            Assert.Equal(99u, result.ServerNonce);
        }
    }

    [Fact]
    public async Task Handshake_RejectsAck_OnUnreliableChannel()
    {
        var (client, peer) = LoopbackTransport.CreatePair();
        using (client)
        using (peer)
        {
            var exchange = HandshakeClient.ExchangeAsync(client, 7).AsTask();
            var helloPacket = await peer.ReceiveAsync().ConfigureAwait(true);
            using var codec = new PacketCodec();
            Assert.True(codec.TryDecode(helloPacket.Payload.Span, out var hello));
            var ack = new byte[256];
            var written = codec.WriteHelloAck(ack, ProtocolVersion.Current, 1, hello.ClientNonce);
            await peer.SendAsync(Channel.Sequenced(1), ack.AsMemory(0, written)).ConfigureAwait(true);
            await Assert.ThrowsAsync<ProtocolException>(async () => await exchange.ConfigureAwait(true));
        }
    }

    private readonly record struct LoopPacket(Channel Channel, byte[] Payload);

    private sealed class LoopbackTransport : ITransport
    {
        private readonly System.Threading.Channels.ChannelReader<LoopPacket> _reader;
        private readonly System.Threading.Channels.ChannelWriter<LoopPacket> _writer;
        private byte[] _held = [];

        private LoopbackTransport(
            System.Threading.Channels.ChannelReader<LoopPacket> reader,
            System.Threading.Channels.ChannelWriter<LoopPacket> writer)
        {
            _reader = reader;
            _writer = writer;
        }

        public static (LoopbackTransport Client, LoopbackTransport Peer) CreatePair()
        {
            var clientToPeer = PipeChannel.CreateUnbounded<LoopPacket>();
            var peerToClient = PipeChannel.CreateUnbounded<LoopPacket>();
            return (
                new LoopbackTransport(peerToClient.Reader, clientToPeer.Writer),
                new LoopbackTransport(clientToPeer.Reader, peerToClient.Writer));
        }

        public ValueTask SendAsync(
            Antor.Net.Channel channel,
            ReadOnlyMemory<byte> payload,
            CancellationToken cancellationToken = default)
        {
            return _writer.WriteAsync(new LoopPacket(channel, payload.ToArray()), cancellationToken);
        }

        public async ValueTask<TransportPacket> ReceiveAsync(CancellationToken cancellationToken = default)
        {
            var packet = await _reader.ReadAsync(cancellationToken).ConfigureAwait(false);
            _held = packet.Payload;
            return new TransportPacket(packet.Channel, _held);
        }

        public void Dispose() => _writer.TryComplete();
    }
}
