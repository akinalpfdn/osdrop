using Osdrop.App.Interop;

namespace Osdrop.App.Engine;

/// <summary>This PC in the mesh. IDs are lowercase hex.</summary>
internal sealed record EngineInfo(string DeviceId, string? GroupId, ushort ListenPort);

/// <summary>What a member may do on this PC.</summary>
internal sealed record Permissions(bool Control, bool Files, bool Clipboard);

internal sealed record GroupMember(
    string DeviceId, string Name, Platform Platform, bool IsSelf, bool IsConnected, Permissions Permissions);

/// <summary>A computer no member pairs with until one unblocks it.</summary>
internal sealed record BlockedComputer(string DeviceId, string Name, Platform Platform);

internal sealed record NearbyComputer(
    string DeviceId, string Name, Platform Platform, bool InAnotherGroup, bool NeedsUpdate);

internal sealed record SessionStats(
    uint RoundTripMicroseconds, long ClockOffsetMicroseconds, ulong DatagramsRejected, ulong StaleMotionDropped);

/// <summary>Between a Mac and a PC: which keys the modifiers become (osdrop_input_settings).</summary>
public enum ModifierMapping
{
    /// <summary>Every key as it is: Command is the Windows key.</summary>
    Positions = 0,
    /// <summary>Control and Command swapped, so shortcuts keep their place: Command+C copies on a PC.</summary>
    Shortcuts = 1,
}

/// <summary>The user's input settings, as core takes them.</summary>
internal sealed record InputSettings(
    ModifierMapping Mapping, bool Immediate, byte CrossingModifiers, uint PushMilliseconds);

/// <summary>
/// A chord as OsdropInput's hook matches it: modifier bits (1 Ctrl, 2 Alt, 4 Shift, 8 Windows) and
/// a key by its scan code, so the layout does not change it.
/// </summary>
public sealed record Hotkey(byte Modifiers, ushort ScanCode, bool Extended)
{
    public const byte Control = 1;
    public const byte Alt = 2;
    public const byte Shift = 4;
    public const byte Windows = 8;

    /// <summary>Ctrl+Alt+Win+Esc: the Mac's ⌃⌥⌘⎋ on the same keys.</summary>
    public static Hotkey DefaultReturnHome { get; } = new(Control | Alt | Windows, 0x01, false);

    /// <summary>Ctrl+Alt+Win+K (Windows keeps Win+L for locking the PC).</summary>
    public static Hotkey DefaultLockInput { get; } = new(Control | Alt | Windows, 0x25, false);

    /// <summary>Ctrl, Alt or Windows is held: Shift alone types.</summary>
    public bool HasModifier => (Modifiers & (Control | Alt | Windows)) != 0 && (Modifiers & ~0x0F) == 0;
}

/// <summary>What the engine starts with; a new name restarts it.</summary>
internal sealed record EngineOptions(
    string? DeviceName, bool SharesClipboard, ulong ClipboardFilesLimit, InputSettings Input,
    Hotkey ReturnHotkey, Hotkey LockHotkey, bool RespectsGameCursor);

/// <summary>A monitor's logical frame in the arrangement, in 1/16 of a unit, relative to its computer.</summary>
internal readonly record struct MonitorFrame(int X, int Y, int Width, int Height);

/// <summary>Where a computer's origin sits in the arrangement; detached ones are kept out of crossings.</summary>
internal sealed record Placement(string DeviceId, int X, int Y, bool IsDetached);

/// <summary>A computer whose screens are known; <see cref="Placement"/> is null until it is placed.</summary>
internal sealed record LayoutComputer(string DeviceId, Placement? Placement, IReadOnlyList<MonitorFrame> Monitors);

/// <summary>
/// How the cursor last came back, when it is worth telling: capture_ended's reason (4 refused,
/// 6 the other computer stopped answering) and, when refused, why (1 locked, 2 asleep, 3 not
/// permitted, 4 busy).
/// </summary>
internal sealed record CaptureNotice(byte Reason, byte Refused, string DeviceId);

/// <summary>Where this PC's mouse and keyboard are, and whether other computers can control it.</summary>
internal sealed record InputState(
    bool IsReachable, bool IsLocked, string? ControllerId, string? TargetId, CaptureNotice? Notice)
{
    /// <summary>Before the engine answered: nothing to warn about yet.</summary>
    public static InputState Unknown { get; } = new(IsReachable: true, IsLocked: false, null, null, null);
}

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
    Task<IReadOnlyList<BlockedComputer>> GetBlockedAsync();
    Task<(CoreResult Result, ulong Connection)> PairAsync(string deviceId);
    Task<CoreResult> ConfirmPairingAsync(ulong connection, bool accepted);
    Task<CoreResult> RevokeAsync(string deviceId);
    /// <summary>Blocks a computer everywhere in the group: a member is removed, a computer showing its code is refused.</summary>
    Task<CoreResult> BlockAsync(string deviceId);
    Task<CoreResult> UnblockAsync(string deviceId);
    Task<CoreResult> LeaveGroupAsync();
    /// <summary>Stops fetching copied files; the clipboard keeps what it had.</summary>
    Task CancelClipboardFetchAsync();
    /// <summary>Cancels a transfer this PC receives for a drag; what it wrote goes.</summary>
    Task CancelTransferAsync(ulong transferId);
    /// <summary>Withdraws this PC's drag: the computers fetching its items stop.</summary>
    Task WithdrawDragAsync(uint dragId);

    Task<CoreResult> SetPermissionsAsync(string deviceId, Permissions permissions);
    Task<IReadOnlyList<LayoutComputer>> GetLayoutAsync();
    /// <summary>A new arrangement from the editor; INVALID_LAYOUT when computers overlap or one touches none.</summary>
    Task<CoreResult> SetLayoutAsync(IReadOnlyList<Placement> placements);
    Task<CoreResult> SetInputSettingsAsync(InputSettings settings);
    /// <summary>The pointer speed on a member, in thousandths of this PC's own.</summary>
    Task<CoreResult> SetPointerSpeedAsync(string deviceId, uint permille);
    Task<CoreResult> SetClipboardAsync(bool shared, ulong filesLimit);
    /// <summary>INVALID_ARGUMENT for a chord the hook cannot match.</summary>
    Task<CoreResult> SetHotkeysAsync(Hotkey returnHome, Hotkey lockInput);
    /// <summary>Whether no crossing happens while a game holds the cursor on its monitor.</summary>
    Task<CoreResult> SetRespectsGameCursorAsync(bool respects);
    Task<CoreResult> LockInputAsync(bool locked);
    /// <summary>Developer tool: a drag dropped at an edge comes back at the left edge (Phase 4).</summary>
    Task<CoreResult> SetDragLoopbackAsync(bool enabled);
    Task<InputState> GetInputStateAsync();
    Task DismissInputNoticeAsync();
    /// <summary>Stops and starts the engine with new options (a new name); every session restarts.</summary>
    Task<CoreResult> RestartAsync(EngineOptions options);
}
