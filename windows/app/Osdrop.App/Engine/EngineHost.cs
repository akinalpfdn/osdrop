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
    public static async Task<(CoreResult Result, EngineHost? Host)> StartAsync(Dispatcher dispatcher)
    {
        var host = new EngineHost(dispatcher);
        host._self = GCHandle.Alloc(host);
        nint context = GCHandle.ToIntPtr(host._self);
        (CoreResult result, nint engine) = await Native.Run(() => CreateNative(context)).ConfigureAwait(true);
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

    public Task<CoreResult> LeaveGroupAsync() => Native.Run(() => NativeMethods.LeaveGroup(_engine));

    public Task ScreensChangedAsync() => Native.Run(() => NativeMethods.ScreensChanged(_engine));

    public Task CloseAllAsync(bool sleeping) =>
        Native.Run(() => NativeMethods.CloseAll(_engine, sleeping ? (byte)1 : (byte)0));

    public Task ResumeAsync() => Native.Run(() => NativeMethods.Resume(_engine));

    public Task CancelClipboardFetchAsync() => Native.Run(() => NativeMethods.CancelClipboardFetch(_engine));

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

    private static unsafe (CoreResult Result, nint Engine) CreateNative(nint context)
    {
        nint engine;
        CoreResult result = NativeMethods.Create(&OnEvent, context, &engine);
        return (result, engine);
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
                list.Add(new GroupMember(
                    Utf8.Hex(member->DeviceId, NativeSizes.DeviceId), Utf8.Read(member->Name, NativeSizes.Name),
                    member->Platform, member->Self == 1, member->Connected == 1));
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
