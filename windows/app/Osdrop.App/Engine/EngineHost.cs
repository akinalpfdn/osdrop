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

    private static unsafe List<GroupMember> ReadMembers(nint engine)
    {
        nuint count = 0;
        CoreResult counted = NativeMethods.Members(engine, null, 0, &count);
        if (counted is not (CoreResult.Ok or CoreResult.BufferTooSmall) || count == 0)
        {
            return [];
        }
        var members = new NativeMember[(int)count];
        fixed (NativeMember* pointer = members)
        {
            if (NativeMethods.Members(engine, pointer, (nuint)members.Length, &count) != CoreResult.Ok)
            {
                return [];
            }
            var list = new List<GroupMember>((int)count);
            for (int i = 0; i < (int)count; ++i)
            {
                NativeMember* member = pointer + i;
                list.Add(new GroupMember(
                    Utf8.Hex(member->DeviceId, NativeSizes.DeviceId), Utf8.Read(member->Name, NativeSizes.Name),
                    member->Platform, member->Self == 1, member->Connected == 1));
            }
            return list;
        }
    }

    private static unsafe List<NearbyComputer> ReadNearby(nint engine)
    {
        nuint count = 0;
        CoreResult counted = NativeMethods.Nearby(engine, null, 0, &count);
        if (counted is not (CoreResult.Ok or CoreResult.BufferTooSmall) || count == 0)
        {
            return [];
        }
        var computers = new NativeNearbyComputer[(int)count];
        fixed (NativeNearbyComputer* pointer = computers)
        {
            if (NativeMethods.Nearby(engine, pointer, (nuint)computers.Length, &count) != CoreResult.Ok)
            {
                return [];
            }
            var list = new List<NearbyComputer>((int)count);
            for (int i = 0; i < (int)count; ++i)
            {
                NativeNearbyComputer* computer = pointer + i;
                list.Add(new NearbyComputer(
                    Utf8.Hex(computer->DeviceId, NativeSizes.DeviceId), Utf8.Read(computer->Name, NativeSizes.Name),
                    computer->Platform, computer->InAnotherGroup == 1, computer->NeedsUpdate == 1));
            }
            return list;
        }
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
            native->Level, Utf8.Read(native->Message));
        self._dispatcher.BeginInvoke(() => self.EventReceived?.Invoke(received));
    }
}
