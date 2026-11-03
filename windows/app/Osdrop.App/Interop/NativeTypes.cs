using System.Runtime.InteropServices;

namespace Osdrop.App.Interop;

// Mirrors of the plain C types in osdrop.h and engine.h. Values are stable (never renumbered).

/// <summary>osdrop_result: the codes the shell tells apart; any other value is kept as is.</summary>
internal enum CoreResult
{
    Ok = 0,
    BufferTooSmall = 2,
    UnsupportedVersion = 5,
    DecryptionFailed = 11,
    InvalidKey = 15,
    TimedOut = 17,
    ConnectionClosed = 18,
    NotTrusted = 21,
    PairingRejected = 22,
    PeerBusy = 23,
    InvalidState = 24,
    Network = 25,
    Internal = 26,
    PeerSleeping = 27,
    GroupFull = 29,
    InAnotherGroup = 30,
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
}

internal static class NativeSizes
{
    public const int DeviceId = 16;
    public const int GroupId = 16;
    public const int Key = 32;
    public const int Name = 65;
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
