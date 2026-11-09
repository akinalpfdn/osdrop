using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osdrop.App.Interop;

namespace Osdrop.App.Engine;

/// <summary>
/// The native engine: this PC's mesh, group, DNS-SD and stored state. Every call runs on
/// <see cref="Native"/>; events are posted to the dispatcher's thread.
/// </summary>
internal sealed class EngineHost : IGroupEngine, IAsyncDisposable
{
    private const int _listAttempts = 8;

    private readonly Dispatcher _dispatcher;
    private GCHandle _self;
    private nint _engine;

    private EngineHost(Dispatcher dispatcher) => _dispatcher = dispatcher;

    public event Action<EngineEvent>? EventReceived;

    /// <summary>Starts the engine; the result says why it could not.</summary>
    public static async Task<(CoreResult Result, EngineHost? Host)> StartAsync(Dispatcher dispatcher, EngineOptions options)
    {
        var host = new EngineHost(dispatcher);
        host._self = GCHandle.Alloc(host);
        nint context = GCHandle.ToIntPtr(host._self);
        (CoreResult result, nint engine) = await Native.Run(() => CreateNative(context, options)).ConfigureAwait(true);
        if (result != CoreResult.Ok)
        {
            host._self.Free();
            return (result, null);
        }
        host._engine = engine;
        return (result, host);
    }

    public async ValueTask DisposeAsync()
    {
        nint engine = _engine;
        _engine = 0;
        if (engine != 0)
        {
            // No callback runs after this returns.
            await Native.Run(() => NativeMethods.Destroy(engine)).ConfigureAwait(true);
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    public Task<EngineInfo?> GetInfoAsync() => Native.Run(() => ReadInfo(_engine));

    public Task<IReadOnlyList<GroupMember>> GetMembersAsync() =>
        Native.Run<IReadOnlyList<GroupMember>>(() => ReadMembers(_engine));

    public Task<IReadOnlyList<NearbyComputer>> GetNearbyAsync() =>
        Native.Run<IReadOnlyList<NearbyComputer>>(() => ReadNearby(_engine));

    public Task<IReadOnlyList<BlockedComputer>> GetBlockedAsync() =>
        Native.Run<IReadOnlyList<BlockedComputer>>(() => ReadBlocked(_engine));

    public Task<(CoreResult Result, ulong Connection)> PairAsync(string deviceId)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => PairNative(_engine, id));
    }

    public Task<CoreResult> ConfirmPairingAsync(ulong connection, bool accepted) =>
        Native.Run(() => NativeMethods.ConfirmPairing(_engine, connection, accepted ? (byte)1 : (byte)0));

    public Task<CoreResult> RevokeAsync(string deviceId)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => RevokeNative(_engine, id));
    }

    public Task<CoreResult> BlockAsync(string deviceId)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => BlockNative(_engine, id, block: true));
    }

    public Task<CoreResult> UnblockAsync(string deviceId)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => BlockNative(_engine, id, block: false));
    }

    public Task<CoreResult> LeaveGroupAsync() => Native.Run(() => NativeMethods.LeaveGroup(_engine));

    public Task ScreensChangedAsync() => Native.Run(() => NativeMethods.ScreensChanged(_engine));

    public Task CloseAllAsync(bool sleeping) =>
        Native.Run(() => NativeMethods.CloseAll(_engine, sleeping ? (byte)1 : (byte)0));

    public Task ResumeAsync() => Native.Run(() => NativeMethods.Resume(_engine));

    public Task CancelClipboardFetchAsync() => Native.Run(() => NativeMethods.CancelClipboardFetch(_engine));

    public Task<CoreResult> SetPermissionsAsync(string deviceId, Permissions permissions)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => SetPermissionsNative(_engine, id, permissions));
    }

    public Task<IReadOnlyList<LayoutComputer>> GetLayoutAsync() =>
        Native.Run<IReadOnlyList<LayoutComputer>>(() => ReadLayout(_engine));

    public Task<CoreResult> SetLayoutAsync(IReadOnlyList<Placement> placements)
    {
        NativePlacement[] native = [.. placements.Select(ToNative)];
        return Native.Run(() => SetLayoutNative(_engine, native));
    }

    public Task<CoreResult> SetInputSettingsAsync(InputSettings settings) =>
        Native.Run(() => SetInputSettingsNative(_engine, ToNative(settings)));

    public Task<CoreResult> SetPointerSpeedAsync(string deviceId, uint permille)
    {
        byte[] id = Convert.FromHexString(deviceId);
        return Native.Run(() => SetPointerSpeedNative(_engine, id, permille));
    }

    public Task<CoreResult> SetClipboardAsync(bool shared, ulong filesLimit) =>
        Native.Run(() => NativeMethods.SetClipboard(_engine, shared ? (byte)1 : (byte)0, filesLimit));

    public Task<CoreResult> SetHotkeysAsync(Hotkey returnHome, Hotkey lockInput) => Native.Run(() =>
    {
        NativeHotkey home = ToNative(returnHome);
        NativeHotkey lockHotkey = ToNative(lockInput);
        return SetHotkeysNative(_engine, home, lockHotkey);
    });

    public Task<CoreResult> SetRespectsGameCursorAsync(bool respects) =>
        Native.Run(() => NativeMethods.SetRespectsCursorClip(_engine, respects ? (byte)1 : (byte)0));

    public Task<CoreResult> SetDragLoopbackAsync(bool enabled) =>
        Native.Run(() => NativeMethods.SetDragLoopback(_engine, enabled ? (byte)1 : (byte)0));

    public Task<CoreResult> LockInputAsync(bool locked) =>
        Native.Run(() => NativeMethods.LockInput(_engine, locked ? (byte)1 : (byte)0));

    public Task<InputState> GetInputStateAsync() => Native.Run(() => ReadInputState(_engine));

    public Task DismissInputNoticeAsync() => Native.Run(() => NativeMethods.DismissInputNotice(_engine));

    /// <summary>
    /// Stops the engine and starts it again with new options, on the native queue, so no call runs in
    /// between; this host and its subscribers stay. A failed start leaves no engine: calls then fail.
    /// </summary>
    public Task<CoreResult> RestartAsync(EngineOptions options)
    {
        nint context = GCHandle.ToIntPtr(_self);
        return Native.Run(() =>
        {
            nint old = _engine;
            _engine = 0;
            if (old != 0)
            {
                NativeMethods.Destroy(old);
            }
            (CoreResult result, nint engine) = CreateNative(context, options);
            _engine = result == CoreResult.Ok ? engine : 0;
            return result;
        });
    }

    /// <summary>Stops the engine while the app exits: waits on the native worker, never on this thread.</summary>
    public void Shutdown()
    {
        nint engine = _engine;
        _engine = 0;
        if (engine != 0)
        {
            Native.Run(() => NativeMethods.Destroy(engine)).Wait();
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    private static unsafe (CoreResult Result, nint Engine) CreateNative(nint context, EngineOptions options)
    {
        byte[] name = Utf8.Terminated(options.DeviceName ?? string.Empty);
        fixed (byte* namePointer = name)
        {
            var config = new NativeEngineConfig
            {
                DeviceName = namePointer,
                SharesClipboard = options.SharesClipboard ? (byte)1 : (byte)0,
                ClipboardFilesLimit = options.ClipboardFilesLimit,
                Input = ToNative(options.Input),
                ReturnHotkey = ToNative(options.ReturnHotkey),
                LockHotkey = ToNative(options.LockHotkey),
                RespectsCursorClip = options.RespectsGameCursor ? (byte)1 : (byte)0,
            };
            nint engine;
            CoreResult result = NativeMethods.Create(&config, &OnEvent, context, &engine);
            return (result, engine);
        }
    }

    private static NativeHotkey ToNative(Hotkey hotkey) => new()
    {
        Modifiers = hotkey.Modifiers,
        ScanCode = hotkey.ScanCode,
        Extended = hotkey.Extended ? (byte)1 : (byte)0,
    };

    private static unsafe CoreResult SetHotkeysNative(nint engine, NativeHotkey returnHome, NativeHotkey lockInput) =>
        NativeMethods.SetHotkeys(engine, &returnHome, &lockInput);

    private static NativeInputSettings ToNative(InputSettings settings) => new()
    {
        ModifierMapping = (byte)settings.Mapping,
        Immediate = settings.Immediate ? (byte)1 : (byte)0,
        CrossingModifiers = settings.CrossingModifiers,
        PushMilliseconds = settings.PushMilliseconds,
    };

    private static unsafe NativePlacement ToNative(Placement placement)
    {
        var native = new NativePlacement { X = placement.X, Y = placement.Y, Detached = placement.IsDetached ? (byte)1 : (byte)0 };
        Convert.FromHexString(placement.DeviceId).CopyTo(new Span<byte>(native.DeviceId, NativeSizes.DeviceId));
        return native;
    }

    private static unsafe CoreResult SetPermissionsNative(nint engine, byte[] deviceId, Permissions permissions)
    {
        var native = new NativePermissions
        {
            Control = permissions.Control ? (byte)1 : (byte)0,
            Files = permissions.Files ? (byte)1 : (byte)0,
            Clipboard = permissions.Clipboard ? (byte)1 : (byte)0,
        };
        fixed (byte* id = deviceId)
        {
            return NativeMethods.SetPermissions(engine, id, &native);
        }
    }

    private static unsafe CoreResult SetLayoutNative(nint engine, NativePlacement[] placements)
    {
        fixed (NativePlacement* pointer = placements)
        {
            return NativeMethods.SetLayout(engine, pointer, (nuint)placements.Length);
        }
    }

    private static unsafe CoreResult SetInputSettingsNative(nint engine, NativeInputSettings settings) =>
        NativeMethods.SetInputSettings(engine, &settings);

    private static unsafe CoreResult SetPointerSpeedNative(nint engine, byte[] deviceId, uint permille)
    {
        fixed (byte* id = deviceId)
        {
            return NativeMethods.SetPointerSpeed(engine, id, permille);
        }
    }

    private static unsafe InputState ReadInputState(nint engine)
    {
        NativeEngineInputState state;
        if (NativeMethods.GetInputState(engine, &state) != CoreResult.Ok)
        {
            return InputState.Unknown;
        }
        CaptureNotice? notice = state.NoticeReason == 0 ? null
            : new CaptureNotice(state.NoticeReason, state.NoticeRefused, Utf8.Hex(state.NoticeDeviceId, NativeSizes.DeviceId));
        return new InputState(
            state.Reachable == 1, state.Locked == 1,
            state.Controlled == 1 ? Utf8.Hex(state.ControllerId, NativeSizes.DeviceId) : null,
            state.Capturing == 1 ? Utf8.Hex(state.TargetId, NativeSizes.DeviceId) : null,
            notice);
    }

    private static unsafe List<LayoutComputer> ReadLayout(nint engine)
    {
        NativeLayoutComputer[] computers = ReadList<NativeLayoutComputer>(
            (items, capacity, count) => NativeMethods.Layout(engine, items, capacity, count));
        var list = new List<LayoutComputer>(computers.Length);
        fixed (NativeLayoutComputer* pointer = computers)
        {
            for (int i = 0; i < computers.Length; ++i)
            {
                NativeLayoutComputer* computer = pointer + i;
                string deviceId = Utf8.Hex(computer->Placement.DeviceId, NativeSizes.DeviceId);
                Placement? placement = computer->Placed == 1
                    ? new Placement(deviceId, computer->Placement.X, computer->Placement.Y, computer->Placement.Detached == 1)
                    : null;
                int count = Math.Min((int)computer->MonitorCount, NativeSizes.MaxMonitors);
                var monitors = new List<MonitorFrame>(count);
                for (int m = 0; m < count; ++m)
                {
                    NativeRect frame = computer->Monitors[m].Frame;
                    monitors.Add(new MonitorFrame(frame.X, frame.Y, frame.Width, frame.Height));
                }
                list.Add(new LayoutComputer(deviceId, placement, monitors));
            }
        }
        return list;
    }

    private static unsafe EngineInfo? ReadInfo(nint engine)
    {
        NativeEngineInfo info;
        if (NativeMethods.GetInfo(engine, &info) != CoreResult.Ok)
        {
            return null;
        }
        string? group = info.HasGroup == 1 ? Utf8.Hex(info.GroupId, NativeSizes.GroupId) : null;
        return new EngineInfo(Utf8.Hex(info.DeviceId, NativeSizes.DeviceId), group, info.ListenPort);
    }

    private unsafe delegate CoreResult ListReader<T>(T* items, nuint capacity, nuint* count)
        where T : unmanaged;

    /// <summary>
    /// One of the engine's lists, counted and then read; read again when it grew in between, because the
    /// engine keeps working on its own threads and a computer can join after the count.
    /// </summary>
    private static unsafe T[] ReadList<T>(ListReader<T> read)
        where T : unmanaged
    {
        for (int attempt = 0; attempt < _listAttempts; ++attempt)
        {
            nuint count = 0;
            CoreResult counted = read(null, 0, &count);
            if (counted is not (CoreResult.Ok or CoreResult.BufferTooSmall) || count == 0)
            {
                return [];
            }
            var items = new T[(int)count];
            fixed (T* pointer = items)
            {
                CoreResult listed = read(pointer, (nuint)items.Length, &count);
                if (listed == CoreResult.BufferTooSmall)
                {
                    continue;
                }
                return listed == CoreResult.Ok ? items[..(int)count] : [];
            }
        }
        return [];
    }

    private static unsafe List<GroupMember> ReadMembers(nint engine)
    {
        NativeMember[] members = ReadList<NativeMember>(
            (items, capacity, count) => NativeMethods.Members(engine, items, capacity, count));
        var list = new List<GroupMember>(members.Length);
        fixed (NativeMember* pointer = members)
        {
            for (int i = 0; i < members.Length; ++i)
            {
                NativeMember* member = pointer + i;
                NativePermissions permissions = member->Permissions;
                list.Add(new GroupMember(
                    Utf8.Hex(member->DeviceId, NativeSizes.DeviceId), Utf8.Read(member->Name, NativeSizes.Name),
                    member->Platform, member->Self == 1, member->Connected == 1,
                    new Permissions(permissions.Control == 1, permissions.Files == 1, permissions.Clipboard == 1)));
            }
        }
        return list;
    }

    private static unsafe List<NearbyComputer> ReadNearby(nint engine)
    {
        NativeNearbyComputer[] computers = ReadList<NativeNearbyComputer>(
            (items, capacity, count) => NativeMethods.Nearby(engine, items, capacity, count));
        var list = new List<NearbyComputer>(computers.Length);
        fixed (NativeNearbyComputer* pointer = computers)
        {
            for (int i = 0; i < computers.Length; ++i)
            {
                NativeNearbyComputer* computer = pointer + i;
                list.Add(new NearbyComputer(
                    Utf8.Hex(computer->DeviceId, NativeSizes.DeviceId), Utf8.Read(computer->Name, NativeSizes.Name),
                    computer->Platform, computer->InAnotherGroup == 1, computer->NeedsUpdate == 1));
            }
        }
        return list;
    }

    private static unsafe List<BlockedComputer> ReadBlocked(nint engine)
    {
        NativeBlockedDevice[] devices = ReadList<NativeBlockedDevice>(
            (items, capacity, count) => NativeMethods.Blocked(engine, items, capacity, count));
        var list = new List<BlockedComputer>(devices.Length);
        fixed (NativeBlockedDevice* pointer = devices)
        {
            for (int i = 0; i < devices.Length; ++i)
            {
                NativeBlockedDevice* device = pointer + i;
                list.Add(new BlockedComputer(
                    Utf8.Hex(device->DeviceId, NativeSizes.DeviceId), Utf8.Read(device->Name, NativeSizes.Name),
                    device->Platform));
            }
        }
        return list;
    }

    private static unsafe (CoreResult Result, ulong Connection) PairNative(nint engine, byte[] deviceId)
    {
        ulong connection = 0;
        fixed (byte* id = deviceId)
        {
            CoreResult result = NativeMethods.Pair(engine, id, &connection);
            return (result, connection);
        }
    }

    private static unsafe CoreResult RevokeNative(nint engine, byte[] deviceId)
    {
        fixed (byte* id = deviceId)
        {
            return NativeMethods.Revoke(engine, id);
        }
    }

    private static unsafe CoreResult BlockNative(nint engine, byte[] deviceId, bool block)
    {
        fixed (byte* id = deviceId)
        {
            return block ? NativeMethods.Block(engine, id) : NativeMethods.Unblock(engine, id);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void OnEvent(nint context, NativeEngineEvent* native)
    {
        if (GCHandle.FromIntPtr(context).Target is not EngineHost self)
        {
            return;
        }
        NativeSessionStats stats = native->Stats;
        var received = new EngineEvent(
            native->Kind, native->Reason, native->Code, native->Connection,
            Utf8.Hex(native->DeviceId, NativeSizes.DeviceId), Utf8.Read(native->Name, NativeSizes.Name),
            native->Responsive == 1,
            new SessionStats(
                stats.RoundTripMicroseconds, stats.ClockOffsetMicroseconds, stats.DatagramsRejected,
                stats.StaleMotionDropped),
            native->Level, Utf8.Read(native->Message), native->BytesDone, native->BytesTotal);
        self._dispatcher.BeginInvoke(() => self.EventReceived?.Invoke(received));
    }
}
