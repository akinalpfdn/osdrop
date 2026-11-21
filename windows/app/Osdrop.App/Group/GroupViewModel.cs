using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Engine;
using Osdrop.App.Interop;
using Osdrop.App.Transfers;

namespace Osdrop.App.Group;

/// <summary>
/// The group on this PC: members, nearby computers, pairings, the clipboard's notices and what the
/// mesh window shows. Engine events arrive on the UI thread.
/// </summary>
public sealed partial class GroupViewModel : ObservableObject
{
    private readonly IGroupEngine _engine;
    private ulong? _pairingConnection;
    private string? _pairingPeer;

    internal GroupViewModel(IGroupEngine engine)
    {
        _engine = engine;
        _engine.EventReceived += OnEvent;
    }

    public ObservableCollection<MemberItem> Members { get; } = [];
    public ObservableCollection<NearbyItem> Nearby { get; } = [];
    public ObservableCollection<BlockedItem> Blocked { get; } = [];

    [ObservableProperty]
    public partial bool HasBlocked { get; set; }

    [ObservableProperty]
    public partial PairingViewModel? Pairing { get; set; }

    /// <summary>A member removed this PC from its group.</summary>
    [ObservableProperty]
    public partial bool WasRemoved { get; set; }

    /// <summary>Why another computer's copy is not on the clipboard; null when there is nothing to say.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasClipboardNotice))]
    public partial string? ClipboardNotice { get; set; }

    public bool HasClipboardNotice => ClipboardNotice is not null;

    /// <summary>Copied files on their way from another computer; null when none are.</summary>
    [ObservableProperty]
    public partial ClipboardFetchViewModel? ClipboardFetch { get; set; }

    /// <summary>A notification to show: its title and its text.</summary>
    public event Action<string, string>? NotificationRequested;

    [ObservableProperty]
    public partial string DeviceId { get; set; } = string.Empty;

    /// <summary>Null while in no group.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsInGroup))]
    public partial string? GroupId { get; set; }

    [ObservableProperty]
    public partial int ListenPort { get; set; }

    public bool IsInGroup => GroupId is not null;

    /// <summary>Members other than this PC, for Settings.</summary>
    public IEnumerable<MemberItem> Others => Members.Where(member => !member.IsSelf);

    public bool HasOthers => Members.Any(member => !member.IsSelf);

    /// <summary>Another member has a session: the cursor can cross.</summary>
    public bool HasConnectedOthers => Members.Any(member => !member.IsSelf && member.IsConnected);

    /// <summary>The arrangement or a member's screens changed.</summary>
    public event Action? ArrangementChanged;

    /// <summary>Where this PC's mouse and keyboard are changed.</summary>
    public event Action? InputChanged;

    /// <summary>A member's name, or "another computer" for one this PC does not know.</summary>
    public string NameOf(string deviceId) =>
        Members.FirstOrDefault(member => member.DeviceId == deviceId)?.Name is { Length: > 0 } name
            ? name : Localization.Strings.ClipboardOtherComputer;

    public async Task RefreshAsync()
    {
        await RefreshMembersAsync().ConfigureAwait(true);
        await RefreshNearbyAsync().ConfigureAwait(true);
    }

    [RelayCommand]
    private async Task PairAsync(NearbyItem? computer)
    {
        if (computer is null || (Pairing is not null && Pairing.Stage != PairingStage.Failed))
        {
            return;
        }
        Pairing = new PairingViewModel(this, PairingStage.Dialing, computer.Name);
        _pairingPeer = computer.DeviceId;
        _pairingConnection = null;
        (CoreResult result, ulong connection) = await _engine.PairAsync(computer.DeviceId).ConfigureAwait(true);
        if (result != CoreResult.Ok)
        {
            Pairing = new PairingViewModel(this, PairingStage.Failed, computer.Name, failure: PairingViewModel.Explain(result));
            return;
        }
        _pairingConnection ??= connection;
    }

    [RelayCommand]
    private async Task RevokeAsync(MemberItem? member)
    {
        if (member is null || member.IsSelf)
        {
            return;
        }
        _ = await _engine.RevokeAsync(member.DeviceId).ConfigureAwait(true);
        await RefreshMembersAsync().ConfigureAwait(true);
    }

    /// <summary>Removes the member and blocks it everywhere in the group.</summary>
    [RelayCommand]
    private async Task BlockAsync(MemberItem? member)
    {
        if (member is null || member.IsSelf)
        {
            return;
        }
        _ = await _engine.BlockAsync(member.DeviceId).ConfigureAwait(true);
        await RefreshAsync().ConfigureAwait(true);
    }

    [RelayCommand]
    private async Task UnblockAsync(BlockedItem? computer)
    {
        if (computer is null)
        {
            return;
        }
        _ = await _engine.UnblockAsync(computer.DeviceId).ConfigureAwait(true);
        await RefreshAsync().ConfigureAwait(true);
    }

    [RelayCommand]
    private async Task LeaveAsync()
    {
        _ = await _engine.LeaveGroupAsync().ConfigureAwait(true);
        await RefreshAsync().ConfigureAwait(true);
    }

    [RelayCommand]
    private void DismissRemoval() => WasRemoved = false;

    [RelayCommand]
    private void DismissClipboardNotice() => ClipboardNotice = null;

    /// <summary>Stops fetching copied files; the clipboard keeps what it had.</summary>
    internal void CancelClipboardFetch()
    {
        ClipboardFetch = null;
        _ = _engine.CancelClipboardFetchAsync();
    }

    /// <summary>The user's answer to the code both computers show.</summary>
    internal async Task AnswerAsync(bool accepted)
    {
        if (Pairing is not { Stage: PairingStage.Confirm } shown || _pairingConnection is not ulong connection)
        {
            return;
        }
        Pairing = accepted ? new PairingViewModel(this, PairingStage.Waiting, shown.PeerName, shown.Code) : null;
        CoreResult result = await _engine.ConfirmPairingAsync(connection, accepted).ConfigureAwait(true);
        if (accepted && result != CoreResult.Ok)
        {
            Pairing = new PairingViewModel(this, PairingStage.Failed, shown.PeerName, failure: PairingViewModel.Explain(result));
        }
    }

    /// <summary>Refuses the computer whose code is shown and blocks it everywhere in the group.</summary>
    internal async Task BlockPairingAsync()
    {
        if (Pairing is not { Stage: PairingStage.Confirm } || _pairingPeer is not string peer)
        {
            return;
        }
        Pairing = null;
        _pairingPeer = null;
        _pairingConnection = null;
        _ = await _engine.BlockAsync(peer).ConfigureAwait(true);
        await RefreshAsync().ConfigureAwait(true);
    }

    /// <summary>Closes the pairing window; a code still on screen is rejected.</summary>
    internal void ClosePairing()
    {
        if (Pairing?.Stage == PairingStage.Confirm)
        {
            _ = AnswerAsync(accepted: false);
        }
        Pairing = null;
        _pairingPeer = null;
        _pairingConnection = null;
    }

    private void OnEvent(EngineEvent received)
    {
        switch (received.Kind)
        {
            case EngineEventKind.GroupChanged:
                _ = RefreshMembersAsync();
                break;
            case EngineEventKind.NearbyChanged:
                _ = RefreshNearbyAsync();
                break;
            case EngineEventKind.LeftGroup:
                WasRemoved = received.Reason == CoreResult.NotTrusted;
                _ = RefreshAsync();
                break;
            case EngineEventKind.PairingCode:
                _pairingConnection = received.Connection;
                _pairingPeer = received.DeviceId;
                Pairing = new PairingViewModel(this, PairingStage.Confirm, received.Name, received.Code);
                break;
            case EngineEventKind.PairingFailed:
                if (received.Connection == _pairingConnection && Pairing is not null)
                {
                    Pairing = new PairingViewModel(
                        this, PairingStage.Failed, Pairing.PeerName, failure: PairingViewModel.Explain(received.Reason));
                    _pairingConnection = null;
                }
                break;
            case EngineEventKind.PeerConnected:
                if (received.DeviceId == _pairingPeer && Pairing?.Stage != PairingStage.Failed)
                {
                    Pairing = null;
                    _pairingPeer = null;
                    _pairingConnection = null;
                }
                break;
            case EngineEventKind.PeerResponsive:
                if (Find(received.DeviceId) is MemberItem responsive)
                {
                    responsive.IsResponsive = received.Responsive;
                }
                break;
            case EngineEventKind.Stats:
                if (Find(received.DeviceId) is MemberItem measured)
                {
                    measured.Stats = received.Stats;
                }
                break;
            case EngineEventKind.Log:
                Diagnostics.Log.Write(received.Level, received.Message);
                break;
            case EngineEventKind.LayoutChanged:
                ArrangementChanged?.Invoke();
                break;
            case EngineEventKind.InputChanged:
                InputChanged?.Invoke();
                break;
            case EngineEventKind.ClipboardUnavailable:
                ClipboardNotice = Describe(Localization.Strings.ClipboardUnavailable, received.Name);
                NotificationRequested?.Invoke(Localization.Strings.ClipboardNoticeTitle, ClipboardNotice);
                break;
            case EngineEventKind.ClipboardTooLarge:
                ClipboardNotice = Describe(Localization.Strings.ClipboardTooLarge, received.Name);
                NotificationRequested?.Invoke(Localization.Strings.ClipboardNoticeTitle, ClipboardNotice);
                break;
            case EngineEventKind.ClipboardFetching:
                ClipboardFetch = new ClipboardFetchViewModel(
                    this, NameOrOther(received.Name), received.BytesDone, received.BytesTotal);
                break;
            case EngineEventKind.ClipboardFetchEnded:
                ClipboardFetch = null;
                break;
            case EngineEventKind.ClipboardReady:
                string ready = Describe(Localization.Strings.ClipboardReady, received.Name);
                if (received.Code > 0)
                {
                    ready += Environment.NewLine + TransferRow.LinksNoteFor(received.Code);
                }
                NotificationRequested?.Invoke(Localization.Strings.ClipboardReadyTitle, ready);
                break;
            default:
                break;
        }
    }

    private static string Describe(string text, string name) =>
        string.Format(CultureInfo.CurrentCulture, text, NameOrOther(name));

    private static string NameOrOther(string name) =>
        string.IsNullOrEmpty(name) ? Localization.Strings.ClipboardOtherComputer : name;

    /// <summary>A permission switched in Settings: core tells the member at once.</summary>
    private void OnPermissionsChanged(MemberItem member) => _ = SetPermissionsAsync(member);

    private async Task SetPermissionsAsync(MemberItem member)
    {
        CoreResult result = await _engine.SetPermissionsAsync(member.DeviceId, member.Permissions).ConfigureAwait(true);
        if (result != CoreResult.Ok)
        {
            Diagnostics.Log.Write(LogLevel.Warning, $"Changing a member's permissions failed: {result}");
            await RefreshMembersAsync().ConfigureAwait(true); // show what core has
        }
    }

    private MemberItem? Find(string deviceId) => Members.FirstOrDefault(member => member.DeviceId == deviceId);

    private async Task RefreshMembersAsync()
    {
        EngineInfo? info = await _engine.GetInfoAsync().ConfigureAwait(true);
        if (info is not null)
        {
            DeviceId = info.DeviceId;
            GroupId = info.GroupId;
            ListenPort = info.ListenPort;
        }
        IReadOnlyList<GroupMember> members = await _engine.GetMembersAsync().ConfigureAwait(true);
        Members.Clear();
        foreach (GroupMember member in members)
        {
            Members.Add(new MemberItem(member, OnPermissionsChanged));
        }
        OnPropertyChanged(nameof(Others));
        OnPropertyChanged(nameof(HasOthers));
        OnPropertyChanged(nameof(HasConnectedOthers));
        IReadOnlyList<BlockedComputer> blocked = await _engine.GetBlockedAsync().ConfigureAwait(true);
        Blocked.Clear();
        foreach (BlockedComputer computer in blocked)
        {
            Blocked.Add(new BlockedItem(computer));
        }
        HasBlocked = Blocked.Count > 0;
    }

    private async Task RefreshNearbyAsync()
    {
        IReadOnlyList<NearbyComputer> nearby = await _engine.GetNearbyAsync().ConfigureAwait(true);
        Nearby.Clear();
        foreach (NearbyComputer computer in nearby)
        {
            Nearby.Add(new NearbyItem(computer));
        }
    }
}
