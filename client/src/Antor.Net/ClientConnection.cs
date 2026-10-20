namespace Antor.Net;

/// <summary>
/// TCP session: a <see cref="FramedTransport"/> over a <see cref="TcpBytePipe"/>.
/// The handshake goes through <see cref="ITransport"/> via <see cref="HandshakeClient"/>.
/// </summary>
public sealed class ClientConnection : IDisposable
{
    private readonly ITransport _transport;
    private bool _disposed;

    private ClientConnection(ITransport transport, HandshakeResult handshake)
    {
        _transport = transport;
        Handshake = handshake;
    }

    public HandshakeResult Handshake { get; }

    public static async Task<ClientConnection> ConnectAndHandshakeAsync(
        string host,
        int port,
        uint clientNonce,
        CancellationToken cancellationToken = default)
    {
        var pipe = await TcpBytePipe.ConnectAsync(host, port, cancellationToken).ConfigureAwait(false);
        FramedTransport? transport = null;
        try
        {
            transport = new FramedTransport(pipe);
            var handshake = await HandshakeClient.ExchangeAsync(transport, clientNonce, cancellationToken)
                .ConfigureAwait(false);
            var connection = new ClientConnection(transport, handshake);
            transport = null;
            return connection;
        }
        catch
        {
            if (transport is null)
            {
                pipe.Dispose();
            }
            else
            {
                transport.Dispose();
            }

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
        _transport.Dispose();
    }
}
