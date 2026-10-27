namespace Antor.Protocol;

/// <summary>
/// Values that must stay aligned with <c>protocol/antor.fbs</c> and
/// <c>antor::protocol::kProtocolVersion</c>.
/// </summary>
public static class ProtocolVersion
{
    public const ushort Current = 1;
    public const string FileIdentifier = "ANTR";
}
