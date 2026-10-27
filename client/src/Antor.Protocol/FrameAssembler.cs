using System.Buffers.Binary;

namespace Antor.Protocol;

public enum FrameRead : byte
{
    NeedMore,
    Frame,
    Error,
}

/// <summary>
/// Reassembles TCP bytes into frames inside a caller-owned buffer.
/// A frame returned by <see cref="TryRead"/> stays valid until the next
/// <see cref="WritableRegion"/> call, which may compact the buffer.
/// </summary>
public sealed class FrameAssembler
{
    private readonly byte[] _buffer;
    private int _start;
    private int _end;

    public FrameAssembler(byte[] buffer)
    {
        ArgumentNullException.ThrowIfNull(buffer);
        if (buffer.Length < FrameCodec.HeaderSize + FrameCodec.MaxPayloadLength)
        {
            throw new ArgumentException(
                "Buffer must hold one maximum frame.",
                nameof(buffer));
        }

        _buffer = buffer;
    }

    public int BufferedBytes => _end - _start;

    public Memory<byte> WritableRegion()
    {
        if (_start > 0 && (_start == _end || _buffer.Length - _end < FrameCodec.HeaderSize))
        {
            Compact();
        }

        return _buffer.AsMemory(_end, _buffer.Length - _end);
    }

    public void Commit(int count)
    {
        if (count < 0 || _end > _buffer.Length - count)
        {
            throw new ArgumentOutOfRangeException(nameof(count));
        }

        _end += count;
    }

    public FrameRead TryRead(out int payloadOffset, out int payloadLength)
    {
        payloadOffset = 0;
        payloadLength = 0;
        var available = _end - _start;
        if (available < FrameCodec.HeaderSize)
        {
            return FrameRead.NeedMore;
        }

        var length = BinaryPrimitives.ReadUInt32LittleEndian(_buffer.AsSpan(_start, FrameCodec.HeaderSize));
        if (length == 0 || length > FrameCodec.MaxPayloadLength)
        {
            return FrameRead.Error;
        }

        var total = FrameCodec.HeaderSize + (int)length;
        if (available < total)
        {
            return FrameRead.NeedMore;
        }

        payloadOffset = _start + FrameCodec.HeaderSize;
        payloadLength = (int)length;
        _start += total;
        if (_start == _end)
        {
            _start = 0;
            _end = 0;
        }

        return FrameRead.Frame;
    }

    private void Compact()
    {
        var count = _end - _start;
        if (count > 0 && _start > 0)
        {
            Array.Copy(_buffer, _start, _buffer, 0, count);
        }

        _start = 0;
        _end = count;
    }
}
