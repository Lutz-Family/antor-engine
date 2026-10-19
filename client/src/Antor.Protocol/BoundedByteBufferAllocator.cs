using Google.FlatBuffers;

namespace Antor.Protocol;

/// <summary>
/// FlatBuffers reads <see cref="ByteBuffer.Length"/>, which a normal byte array
/// sets to the full rented size. This allocator reports only the current frame
/// so the verifier cannot walk into adjacent bytes.
/// </summary>
internal sealed class BoundedByteBufferAllocator : ByteBufferAllocator
{
    public BoundedByteBufferAllocator()
    {
        Buffer = [];
        Length = 0;
    }

    public void Assign(byte[] data, int length)
    {
        ArgumentNullException.ThrowIfNull(data);
        if ((uint)length > (uint)data.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(length));
        }

        Buffer = data;
        Length = length;
    }

    public override void GrowFront(int newSize) =>
        throw new NotSupportedException("Decode buffers do not grow.");
}
