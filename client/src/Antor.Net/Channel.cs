namespace Antor.Net;

/// <summary>
/// How a payload should travel. The tag is part of <see cref="ITransport"/> so a
/// later datagram transport can be added without changing the handshake.
/// Only <see cref="Delivery.ReliableOrdered"/> is implemented today.
/// </summary>
public enum Delivery : byte
{
    /// <summary>Lossless and ordered for one session. Sequence stays 0.</summary>
    ReliableOrdered = 0,

    /// <summary>
    /// May be dropped. A newer sequence supersedes an older one for that session.
    /// No transport implements this yet.
    /// </summary>
    UnreliableSequenced = 1,
}

/// <summary>
/// Delivery plus the sequence number a future unreliable transport will use.
/// <see cref="Sequence"/> is ignored for <see cref="Delivery.ReliableOrdered"/>.
/// </summary>
public readonly struct Channel
{
    public Channel(Delivery delivery, uint sequence)
    {
        Delivery = delivery;
        Sequence = sequence;
    }

    public Delivery Delivery { get; }

    public uint Sequence { get; }

    public static Channel Reliable { get; } = new(Delivery.ReliableOrdered, 0);

    public static Channel Sequenced(uint sequence) => new(Delivery.UnreliableSequenced, sequence);
}
