using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Engine;
using Osdrop.App.Interop;

namespace Osdrop.App.Group;

/// <summary>
/// The group on this PC (temporary UI, final in Phase 17): members, nearby computers, pairings and
/// what the mesh window shows. Engine events arrive on the UI thread.
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

    [ObservableProperty]
    public partial string DeviceId { get; set; } = string.Empty;

    /// <summary>Null while in no group.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsInGroup))]
    public partial string? GroupId { get; set; }

    [ObservableProperty]
    public partial int ListenPort { get; set; }

    public bool IsInGroup => GroupId is not null;

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
                System.Diagnostics.Trace.WriteLine(received.Message, "Osdrop");
                break;
            case EngineEventKind.ClipboardUnavailable:
                ClipboardNotice = Describe(Localization.Strings.ClipboardUnavailable, received.Name);
                break;
            case EngineEventKind.ClipboardTooLarge:
                ClipboardNotice = Describe(Localization.Strings.ClipboardTooLarge, received.Name);
                break;
            default:
                break;
        }
    }

    private static string Describe(string text, string name) => string.Format(
        CultureInfo.CurrentCulture, text, string.IsNullOrEmpty(name) ? Localization.Strings.ClipboardOtherComputer : name);

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
            Members.Add(new MemberItem(member));
        }
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
