using System.Buffers;
using System.Net.Sockets;
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
/// One TCP session. Not thread-safe: a single caller awaits operations in order.
/// Receive and send buffers are rented for the life of the connection.
/// Encode and decode go through <see cref="PacketCodec"/>, which does not
/// allocate per message after its first use.
/// </summary>
public sealed class ClientConnection : IDisposable
{
    private readonly Socket _socket;
    private readonly SocketAsyncEventArgs _recvArgs;
    private readonly SocketAsyncEventArgs _sendArgs;
    private readonly SocketSignal _recvSignal = new();
    private readonly SocketSignal _sendSignal = new();
    private readonly byte[] _recv;
    private readonly byte[] _send;
    private readonly FrameAssembler _frames;
    private readonly PacketCodec _codec = new();
    private SocketSignal? _pendingSignal;
    private bool _disposed;

    private ClientConnection()
    {
        _recv = ArrayPool<byte>.Shared.Rent(FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength);
        _send = ArrayPool<byte>.Shared.Rent(FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength);
        _frames = new FrameAssembler(_recv);
        _socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp)
        {
            NoDelay = true,
        };
        _recvArgs = new SocketAsyncEventArgs();
        _sendArgs = new SocketAsyncEventArgs();
        _recvArgs.Completed += (_, args) => Complete(_recvSignal, args);
        _sendArgs.Completed += (_, args) => Complete(_sendSignal, args);
    }

    public HandshakeResult Handshake { get; private set; }

    public static async Task<ClientConnection> ConnectAndHandshakeAsync(
        string host,
        int port,
        uint clientNonce,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrEmpty(host);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(port);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);

        var connection = new ClientConnection();
        try
        {
            await connection._socket.ConnectAsync(host, port, cancellationToken).ConfigureAwait(false);
            var decoded = await connection.ExchangeHelloAsync(clientNonce, cancellationToken)
                .ConfigureAwait(false);
            connection.Handshake = new HandshakeResult(
                decoded.ProtocolVersion,
                decoded.ServerNonce,
                decoded.ClientNonce);
            return connection;
        }
        catch
        {
            connection.Dispose();
            throw;
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _socket.Dispose();
        _recvArgs.Dispose();
        _sendArgs.Dispose();
        _codec.Dispose();
        ArrayPool<byte>.Shared.Return(_recv);
        ArrayPool<byte>.Shared.Return(_send);
    }

    private async Task<DecodedPacket> ExchangeHelloAsync(uint clientNonce, CancellationToken cancellationToken)
    {
        var written = _codec.WriteHello(_send, ProtocolVersion.Current, clientNonce);
        if (written <= 0)
        {
            throw new ProtocolException("Failed to encode Hello.");
        }

        await SendAllAsync(_send.AsMemory(0, written), cancellationToken).ConfigureAwait(false);
        var decoded = await ReceivePacketAsync(cancellationToken).ConfigureAwait(false);
        if (decoded.Kind != PacketKind.HelloAck ||
            decoded.ProtocolVersion != ProtocolVersion.Current ||
            decoded.ClientNonce != clientNonce)
        {
            throw new ProtocolException("Server rejected the handshake.");
        }

        return decoded;
    }

    private async Task<DecodedPacket> ReceivePacketAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            var status = _frames.TryRead(out var offset, out var length);
            if (status == FrameRead.Error)
            {
                throw new ProtocolException("Server sent a malformed frame.");
            }

            if (status == FrameRead.Frame)
            {
                if (!_codec.TryDecode(_recv.AsSpan(offset, length), out var decoded))
                {
                    throw new ProtocolException("Server sent a packet that failed verification.");
                }

                return decoded;
            }

            var region = _frames.WritableRegion();
            if (region.IsEmpty)
            {
                throw new ProtocolException("Receive buffer is full.");
            }

            var read = await ReceiveSomeAsync(region, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                throw new ProtocolException("Server closed the connection.");
            }

            _frames.Commit(read);
        }
    }

    private async ValueTask SendAllAsync(Memory<byte> payload, CancellationToken cancellationToken)
    {
        var remaining = payload;
        while (remaining.Length > 0)
        {
            _sendArgs.SetBuffer(remaining);
            var sent = await ExecuteAsync(_sendSignal, _sendArgs, static (socket, args) => socket.SendAsync(args), cancellationToken)
                .ConfigureAwait(false);
            if (sent <= 0)
            {
                throw new ProtocolException("Server closed the connection during send.");
            }

            remaining = remaining.Slice(sent);
        }
    }

    private ValueTask<int> ReceiveSomeAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        _recvArgs.SetBuffer(buffer);
        return ExecuteAsync(_recvSignal, _recvArgs, static (socket, args) => socket.ReceiveAsync(args), cancellationToken);
    }

    private async ValueTask<int> ExecuteAsync(
        SocketSignal signal,
        SocketAsyncEventArgs args,
        Func<Socket, SocketAsyncEventArgs, bool> start,
        CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        cancellationToken.ThrowIfCancellationRequested();
        signal.Reset();
        if (!start(_socket, args))
        {
            if (args.SocketError != SocketError.Success)
            {
                throw new ProtocolException($"Socket error: {args.SocketError}.");
            }

            return args.BytesTransferred;
        }

        return await WaitAsync(signal, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<int> WaitAsync(SocketSignal signal, CancellationToken cancellationToken)
    {
        if (!cancellationToken.CanBeCanceled)
        {
            return await signal.Wait().ConfigureAwait(false);
        }

        _pendingSignal = signal;
        using var registration = cancellationToken.Register(static state =>
        {
            var self = (ClientConnection)state!;
            self._pendingSignal?.TryFail(new OperationCanceledException());
            try
            {
                self._socket.Close();
            }
            catch (ObjectDisposedException)
            {
            }
        }, this);
        try
        {
            return await signal.Wait().ConfigureAwait(false);
        }
        finally
        {
            _pendingSignal = null;
        }
    }

    private static void Complete(SocketSignal signal, SocketAsyncEventArgs args)
    {
        if (args.SocketError != SocketError.Success)
        {
            signal.TryFail(new ProtocolException($"Socket error: {args.SocketError}."));
            return;
        }

        signal.TrySucceed(args.BytesTransferred);
    }
}
