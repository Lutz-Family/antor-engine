namespace Antor.Net;

/// <summary>
/// Raw bytes in one direction pair. No framing and no delivery tag.
/// <see cref="SendAsync"/> writes the whole buffer or throws. The buffer stays
/// valid until that task completes. It is <see cref="Memory{T}"/> because the
/// socket completion args require a writable buffer; the pipe does not modify it.
/// <see cref="ReceiveAsync"/> may return a short read; zero means the peer closed.
/// </summary>
public interface IBytePipe : IDisposable
{
    ValueTask SendAsync(Memory<byte> bytes, CancellationToken cancellationToken = default);

    ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default);
}
