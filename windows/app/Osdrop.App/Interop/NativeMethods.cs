using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Osdrop.App.Interop;

/// <summary>The engine's flat C API (windows/engine/include/osdrop/engine.h).</summary>
internal static unsafe partial class NativeMethods
{
    private const string _engine = "OsdropEngine";

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_core_version")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial NativeVersion CoreVersion();

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_create")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Create(
        delegate* unmanaged[Cdecl]<nint, NativeEngineEvent*, void> callback, nint context, nint* engine);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial void Destroy(nint engine);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_get_info")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult GetInfo(nint engine, NativeEngineInfo* info);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_members")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Members(nint engine, NativeMember* members, nuint capacity, nuint* count);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_nearby")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Nearby(
        nint engine, NativeNearbyComputer* computers, nuint capacity, nuint* count);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_pair")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Pair(nint engine, byte* deviceId, ulong* connection);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_confirm_pairing")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult ConfirmPairing(nint engine, ulong connection, byte accepted);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_revoke")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Revoke(nint engine, byte* deviceId);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_leave_group")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult LeaveGroup(nint engine);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_screens_changed")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult ScreensChanged(nint engine);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_close_all")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult CloseAll(nint engine, byte sleeping);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_cancel_clipboard_fetch")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult CancelClipboardFetch(nint engine);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_resume")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Resume(nint engine);
}
