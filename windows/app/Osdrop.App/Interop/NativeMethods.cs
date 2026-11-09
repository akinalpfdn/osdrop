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
        NativeEngineConfig* config, delegate* unmanaged[Cdecl]<nint, NativeEngineEvent*, void> callback,
        nint context, nint* engine);

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

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_block")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Block(nint engine, byte* deviceId);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_unblock")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Unblock(nint engine, byte* deviceId);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_blocked")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Blocked(
        nint engine, NativeBlockedDevice* devices, nuint capacity, nuint* count);

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

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_clipboard")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetClipboard(nint engine, byte shared, ulong filesLimit);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_permissions")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetPermissions(nint engine, byte* deviceId, NativePermissions* permissions);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_layout")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult Layout(
        nint engine, NativeLayoutComputer* computers, nuint capacity, nuint* count);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_layout")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetLayout(nint engine, NativePlacement* placements, nuint count);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_input_settings")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetInputSettings(nint engine, NativeInputSettings* settings);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_pointer_speed")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetPointerSpeed(nint engine, byte* deviceId, uint permille);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_hotkeys")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetHotkeys(nint engine, NativeHotkey* returnHotkey, NativeHotkey* lockHotkey);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_set_respects_cursor_clip")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult SetRespectsCursorClip(nint engine, byte respects);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_lock_input")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult LockInput(nint engine, byte locked);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_get_input_state")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult GetInputState(nint engine, NativeEngineInputState* state);

    [LibraryImport(_engine, EntryPoint = "osdrop_engine_dismiss_input_notice")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial CoreResult DismissInputNotice(nint engine);
}
