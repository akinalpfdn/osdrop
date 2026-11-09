using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Osdrop.App.Interop;

// Mirrors of the plain C types in osdrop.h and engine.h. Values are stable (never renumbered).

/// <summary>osdrop_result; a value added later is kept as is.</summary>
internal enum CoreResult
{
    Ok = 0,
    InvalidArgument = 1,
    BufferTooSmall = 2,
    Truncated = 3,
    BadMagic = 4,
    UnsupportedVersion = 5,
    UnknownType = 6,
    TrailingBytes = 7,
    ValueOutOfRange = 8,
    InvalidString = 9,
    TooLarge = 10,
    DecryptionFailed = 11,
    HandshakeOutOfOrder = 12,
    Replayed = 13,
    NonceExhausted = 14,
    InvalidKey = 15,
    RandomUnavailable = 16,
    TimedOut = 17,
    ConnectionClosed = 18,
    PeerError = 19,
    UnexpectedMessage = 20,
    NotTrusted = 21,
    PairingRejected = 22,
    PeerBusy = 23,
    InvalidState = 24,
    Network = 25,
    Internal = 26,
    PeerSleeping = 27,
    NotPermitted = 28,
    GroupFull = 29,
    InAnotherGroup = 30,
    InvalidLayout = 31,
    Cancelled = 32,
    IoFailure = 33,
    NoSpace = 34,
    IntegrityFailed = 35,
    NotFound = 36,
}

/// <summary>osdrop_platform.</summary>
internal enum Platform
{
    MacOS = 1,
    Windows = 2,
}

/// <summary>osdrop_log_level.</summary>
internal enum LogLevel
{
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
}

/// <summary>osdrop_engine_event_kind.</summary>
internal enum EngineEventKind
{
    GroupChanged = 1,
    NearbyChanged = 2,
    LeftGroup = 3,
    PairingCode = 4,
    PairingFailed = 5,
    PeerConnected = 6,
    PeerDisconnected = 7,
    PeerResponsive = 8,
    Stats = 9,
    Log = 10,
    ClipboardUnavailable = 11,
    ClipboardTooLarge = 12,
    ClipboardFetching = 13,
    ClipboardFetchEnded = 14,
    ClipboardReady = 15,
    LayoutChanged = 16,
    InputChanged = 17,
}

internal static class NativeSizes
{
    public const int DeviceId = 16;
    public const int GroupId = 16;
    public const int Key = 32;
    public const int Name = 65;
    public const int MonitorKey = 16;
    public const int MaxMonitors = 16;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeVersion
{
    public ushort Major;
    public ushort Minor;
    public ushort Patch;
}

/// <summary>osdrop_session_stats.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NativeSessionStats
{
    public uint RoundTripMicroseconds;
    public long ClockOffsetMicroseconds;
    public ulong DatagramsRejected;
    public ulong StaleMotionDropped;
}

/// <summary>osdrop_engine_event.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEngineEvent
{
    public EngineEventKind Kind;
    public CoreResult Reason;
    public LogLevel Level;
    public uint Code;
    public ulong Connection;
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public byte Responsive;
    public fixed byte Name[NativeSizes.Name];
    public NativeSessionStats Stats;
    public byte* Message;
    public ulong BytesDone;
    public ulong BytesTotal;
}

/// <summary>osdrop_engine_info.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEngineInfo
{
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public fixed byte GroupId[NativeSizes.GroupId];
    public byte HasGroup;
    public ushort ListenPort;
}

/// <summary>osdrop_nearby_computer.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeNearbyComputer
{
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public Platform Platform;
    public byte InAnotherGroup;
    public byte NeedsUpdate;
    public fixed byte Name[NativeSizes.Name];
}

/// <summary>osdrop_blocked_device.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBlockedDevice
{
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public Platform Platform;
    public fixed byte Name[NativeSizes.Name];
}

/// <summary>osdrop_permissions.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NativePermissions
{
    public byte Control;
    public byte Files;
    public byte Clipboard;
}

/// <summary>osdrop_member.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeMember
{
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public fixed byte PublicKey[NativeSizes.Key];
    public Platform Platform;
    public fixed byte Name[NativeSizes.Name];
    public NativePermissions Permissions;
    public byte Self;
    public byte Connected;
}

/// <summary>osdrop_input_settings.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NativeInputSettings
{
    public byte ModifierMapping;
    public byte Immediate;
    public byte CrossingModifiers;
    public uint PushMilliseconds;
}

/// <summary>osdrop_engine_config.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEngineConfig
{
    public byte* DeviceName;
    public byte SharesClipboard;
    public ulong ClipboardFilesLimit;
    public NativeInputSettings Input;
}

/// <summary>osdrop_engine_input_state.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEngineInputState
{
    public byte Reachable;
    public byte Locked;
    public byte Controlled;
    public fixed byte ControllerId[NativeSizes.DeviceId];
    public byte Capturing;
    public fixed byte TargetId[NativeSizes.DeviceId];
    public byte NoticeReason;
    public byte NoticeRefused;
    public fixed byte NoticeDeviceId[NativeSizes.DeviceId];
}

/// <summary>osdrop_rect.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NativeRect
{
    public int X;
    public int Y;
    public int Width;
    public int Height;
}

/// <summary>osdrop_monitor.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeMonitor
{
    public fixed byte Key[NativeSizes.MonitorKey];
    public NativeRect Frame;
    public NativeRect Native;
    public ushort PixelWidth;
    public ushort PixelHeight;
    public byte Primary;
}

/// <summary>osdrop_placement.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePlacement
{
    public fixed byte DeviceId[NativeSizes.DeviceId];
    public int X;
    public int Y;
    public byte Detached;
}

/// <summary>osdrop_layout_computer.monitors.</summary>
[InlineArray(NativeSizes.MaxMonitors)]
internal struct NativeMonitors
{
    private NativeMonitor _first;
}

/// <summary>osdrop_layout_computer.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NativeLayoutComputer
{
    public byte Placed;
    public NativePlacement Placement;
    public byte MonitorCount;
    public NativeMonitors Monitors;
}
