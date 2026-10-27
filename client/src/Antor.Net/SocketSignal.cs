using System.Threading.Tasks.Sources;

namespace Antor.Net;

/// <summary>
/// Reused completion source for one in-flight socket operation.
/// <see cref="ManualResetValueTaskSourceCore{TResult}"/> posts the continuation
/// asynchronously so user code does not run on the I/O thread.
/// </summary>
internal sealed class SocketSignal : IValueTaskSource<int>
{
    private ManualResetValueTaskSourceCore<int> _core = new()
    {
        RunContinuationsAsynchronously = true,
    };

    private int _gate;

    public void Reset()
    {
        _core.Reset();
        Volatile.Write(ref _gate, 0);
    }

    public bool TrySucceed(int bytes) => TryComplete(null, bytes);

    public bool TryFail(Exception error) => TryComplete(error, 0);

    public ValueTask<int> Wait() => new(this, _core.Version);

    public int GetResult(short token) => _core.GetResult(token);

    public ValueTaskSourceStatus GetStatus(short token) => _core.GetStatus(token);

    public void OnCompleted(
        Action<object?> continuation,
        object? state,
        short token,
        ValueTaskSourceOnCompletedFlags flags) =>
        _core.OnCompleted(continuation, state, token, flags);

    private bool TryComplete(Exception? error, int bytes)
    {
        if (Interlocked.Exchange(ref _gate, 1) != 0)
        {
            return false;
        }

        if (error is null)
        {
            _core.SetResult(bytes);
        }
        else
        {
            _core.SetException(error);
        }

        return true;
    }
}
