using Osdrop.App.Interop;

namespace Osdrop.App.Engine;

/// <summary>This PC in the mesh. IDs are lowercase hex.</summary>
internal sealed record EngineInfo(string DeviceId, string? GroupId, ushort ListenPort);

internal sealed record GroupMember(string DeviceId, string Name, Platform Platform, bool IsSelf, bool IsConnected);

internal sealed record NearbyComputer(
    string DeviceId, string Name, Platform Platform, bool InAnotherGroup, bool NeedsUpdate);

internal sealed record SessionStats(
    uint RoundTripMicroseconds, long ClockOffsetMicroseconds, ulong DatagramsRejected, ulong StaleMotionDropped);

/// <summary>One engine event; fields that do not belong to its kind are empty.</summary>
internal sealed record EngineEvent(
    EngineEventKind Kind,
    CoreResult Reason = CoreResult.Ok,
    uint Code = 0,
    ulong Connection = 0,
    string DeviceId = "",
    string Name = "",
    bool Responsive = false,
    SessionStats? Stats = null,
    LogLevel Level = LogLevel.Info,
    string Message = "",
    ulong BytesDone = 0,
    ulong BytesTotal = 0);

/// <summary>The engine as the view models use it; events arrive on the UI thread.</summary>
internal interface IGroupEngine
{
    event Action<EngineEvent>? EventReceived;

    Task<EngineInfo?> GetInfoAsync();
    Task<IReadOnlyList<GroupMember>> GetMembersAsync();
    Task<IReadOnlyList<NearbyComputer>> GetNearbyAsync();
    Task<(CoreResult Result, ulong Connection)> PairAsync(string deviceId);
    Task<CoreResult> ConfirmPairingAsync(ulong connection, bool accepted);
    Task<CoreResult> RevokeAsync(string deviceId);
    Task<CoreResult> LeaveGroupAsync();
    /// <summary>Stops fetching copied files; the clipboard keeps what it had.</summary>
    Task CancelClipboardFetchAsync();
}
