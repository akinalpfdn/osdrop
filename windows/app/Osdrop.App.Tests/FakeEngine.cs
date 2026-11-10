using Osdrop.App.Engine;
using Osdrop.App.Interop;

namespace Osdrop.App.Tests;

/// <summary>The engine in memory: it records what the view models ask and raises events on request.</summary>
internal sealed class FakeEngine : IGroupEngine
{
    public event Action<EngineEvent>? EventReceived;

    public EngineInfo? Info { get; set; }
    public List<GroupMember> Members { get; } = [];
    public List<NearbyComputer> Nearby { get; } = [];
    public List<string> Paired { get; } = [];
    public List<(ulong Connection, bool Accepted)> Answers { get; } = [];
    public List<string> Revoked { get; } = [];
    public List<BlockedComputer> Blocked { get; } = [];
    public List<string> BlockedIds { get; } = [];
    public List<string> Unblocked { get; } = [];

    public void Raise(EngineEvent received) => EventReceived?.Invoke(received);

    public Task<EngineInfo?> GetInfoAsync() => Task.FromResult(Info);
    public Task<IReadOnlyList<GroupMember>> GetMembersAsync() => Task.FromResult<IReadOnlyList<GroupMember>>([.. Members]);
    public Task<IReadOnlyList<NearbyComputer>> GetNearbyAsync() => Task.FromResult<IReadOnlyList<NearbyComputer>>([.. Nearby]);
    public Task<IReadOnlyList<BlockedComputer>> GetBlockedAsync() => Task.FromResult<IReadOnlyList<BlockedComputer>>([.. Blocked]);

    public Task<(CoreResult Result, ulong Connection)> PairAsync(string deviceId)
    {
        Paired.Add(deviceId);
        return Task.FromResult((CoreResult.Ok, 5UL));
    }

    public Task<CoreResult> ConfirmPairingAsync(ulong connection, bool accepted)
    {
        Answers.Add((connection, accepted));
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> RevokeAsync(string deviceId)
    {
        Revoked.Add(deviceId);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> BlockAsync(string deviceId)
    {
        BlockedIds.Add(deviceId);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> UnblockAsync(string deviceId)
    {
        Unblocked.Add(deviceId);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> LeaveGroupAsync() => Task.FromResult(CoreResult.Ok);

    public int FetchCancels { get; private set; }

    public Task CancelClipboardFetchAsync()
    {
        FetchCancels++;
        return Task.CompletedTask;
    }

    public static Permissions AllAllowed { get; } = new(Control: true, Files: true, Clipboard: true);

    public List<(string DeviceId, Permissions Permissions)> PermissionChanges { get; } = [];
    public List<LayoutComputer> Layout { get; } = [];
    public List<IReadOnlyList<Placement>> Arrangements { get; } = [];
    public CoreResult ArrangementResult { get; set; } = CoreResult.Ok;
    public List<InputSettings> InputSettings { get; } = [];
    public List<(string DeviceId, uint Permille)> PointerSpeeds { get; } = [];
    public List<(bool Shared, ulong FilesLimit)> ClipboardSettings { get; } = [];
    public List<bool> Locks { get; } = [];
    public List<(Hotkey ReturnHome, Hotkey LockInput)> Hotkeys { get; } = [];
    public CoreResult HotkeyResult { get; set; } = CoreResult.Ok;
    public InputState InputState { get; set; } = InputState.Unknown;
    public int NoticeDismissals { get; private set; }
    public List<EngineOptions> Restarts { get; } = [];

    public Task<CoreResult> SetPermissionsAsync(string deviceId, Permissions permissions)
    {
        PermissionChanges.Add((deviceId, permissions));
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<IReadOnlyList<LayoutComputer>> GetLayoutAsync() => Task.FromResult<IReadOnlyList<LayoutComputer>>([.. Layout]);

    public Task<CoreResult> SetLayoutAsync(IReadOnlyList<Placement> placements)
    {
        Arrangements.Add(placements);
        return Task.FromResult(ArrangementResult);
    }

    public Task<CoreResult> SetInputSettingsAsync(InputSettings settings)
    {
        InputSettings.Add(settings);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> SetPointerSpeedAsync(string deviceId, uint permille)
    {
        PointerSpeeds.Add((deviceId, permille));
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> SetClipboardAsync(bool shared, ulong filesLimit)
    {
        ClipboardSettings.Add((shared, filesLimit));
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> SetHotkeysAsync(Hotkey returnHome, Hotkey lockInput)
    {
        Hotkeys.Add((returnHome, lockInput));
        return Task.FromResult(HotkeyResult);
    }

    public List<bool> GameCursorSettings { get; } = [];

    public Task<CoreResult> SetRespectsGameCursorAsync(bool respects)
    {
        GameCursorSettings.Add(respects);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<CoreResult> SetDragLoopbackAsync(bool enabled) => Task.FromResult(CoreResult.Ok);

    public Task<CoreResult> LockInputAsync(bool locked)
    {
        Locks.Add(locked);
        return Task.FromResult(CoreResult.Ok);
    }

    public Task<InputState> GetInputStateAsync() => Task.FromResult(InputState);

    public Task DismissInputNoticeAsync()
    {
        NoticeDismissals++;
        return Task.CompletedTask;
    }

    public Task<CoreResult> RestartAsync(EngineOptions options)
    {
        Restarts.Add(options);
        return Task.FromResult(CoreResult.Ok);
    }
}
