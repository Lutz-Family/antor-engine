using System.Net.Sockets;
using Antor.Protocol;

namespace Antor.Net;

/// <summary>
/// One TCP socket. This type does not frame and does not know about
/// <see cref="Channel"/>. A single caller awaits operations in order.
/// </summary>
public sealed class TcpBytePipe : IBytePipe
{
    private readonly Socket _socket;
    private readonly SocketAsyncEventArgs _recvArgs;
    private readonly SocketAsyncEventArgs _sendArgs;
    private readonly SocketSignal _recvSignal = new();
    private readonly SocketSignal _sendSignal = new();
    private SocketSignal? _pendingSignal;
    private bool _disposed;

    private TcpBytePipe()
    {
        _socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp)
        {
            NoDelay = true,
        };
        _recvArgs = new SocketAsyncEventArgs();
        _sendArgs = new SocketAsyncEventArgs();
        _recvArgs.Completed += (_, args) => Complete(_recvSignal, args);
        _sendArgs.Completed += (_, args) => Complete(_sendSignal, args);
    }

    public static async ValueTask<TcpBytePipe> ConnectAsync(
        string host,
        int port,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrEmpty(host);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(port);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);

        var pipe = new TcpBytePipe();
        try
        {
            await pipe._socket.ConnectAsync(host, port, cancellationToken).ConfigureAwait(false);
            return pipe;
        }
        catch
        {
            pipe.Dispose();
            throw;
        }
    }

    public async ValueTask SendAsync(Memory<byte> bytes, CancellationToken cancellationToken = default)
    {
        var remaining = bytes;
        while (remaining.Length > 0)
        {
            _sendArgs.SetBuffer(remaining);
            var sent = await ExecuteAsync(
                    _sendSignal,
                    _sendArgs,
                    static (socket, args) => socket.SendAsync(args),
                    cancellationToken)
                .ConfigureAwait(false);
            if (sent <= 0)
            {
                throw new ProtocolException("Peer closed the connection during send.");
            }

            remaining = remaining.Slice(sent);
        }
    }

    public ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
    {
        _recvArgs.SetBuffer(buffer);
        return ExecuteAsync(
            _recvSignal,
            _recvArgs,
            static (socket, args) => socket.ReceiveAsync(args),
            cancellationToken);
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
            var self = (TcpBytePipe)state!;
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
