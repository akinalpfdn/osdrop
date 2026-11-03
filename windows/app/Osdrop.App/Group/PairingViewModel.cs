using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Interop;

namespace Osdrop.App.Group;

public enum PairingStage
{
    /// <summary>This PC is reaching the other computer.</summary>
    Dialing,
    /// <summary>Both computers show the code; the user here decides.</summary>
    Confirm,
    /// <summary>Accepted here; the other computer's user has not answered yet.</summary>
    Waiting,
    Failed,
}

/// <summary>One state of a pairing, as the pairing window shows it (temporary UI, Phase 17).</summary>
public sealed partial class PairingViewModel
{
    private readonly GroupViewModel _owner;

    internal PairingViewModel(GroupViewModel owner, PairingStage stage, string peerName, uint code = 0,
        string failure = "")
    {
        _owner = owner;
        Stage = stage;
        PeerName = peerName;
        Code = code;
        Failure = failure;
    }

    public PairingStage Stage { get; }
    public string PeerName { get; }
    public uint Code { get; }
    public string Failure { get; }

    public string Message => Stage switch
    {
        PairingStage.Dialing => Format(Localization.Strings.PairingDialing),
        PairingStage.Confirm => Format(Localization.Strings.PairingConfirm),
        PairingStage.Waiting => Format(Localization.Strings.PairingWaiting),
        _ => Format(Localization.Strings.PairingFailed),
    };

    /// <summary>"042 917": six digits with leading zeros, in two groups.</summary>
    public string CodeText => Stage is PairingStage.Confirm or PairingStage.Waiting ? Display(Code) : string.Empty;

    public bool ShowsCode => CodeText.Length > 0;

    public string Detail => Stage switch
    {
        PairingStage.Confirm => Localization.Strings.PairingHint,
        PairingStage.Failed => Failure,
        _ => string.Empty,
    };

    public bool HasDetail => Detail.Length > 0;
    public bool CanAnswer => Stage == PairingStage.Confirm;
    public bool CanClose => Stage != PairingStage.Confirm;

    internal static string Display(uint code)
    {
        string digits = (code % 1_000_000).ToString("D6", CultureInfo.InvariantCulture);
        return $"{digits[..3]} {digits[3..]}";
    }

    /// <summary>Why a pairing did not complete, in words.</summary>
    internal static string Explain(CoreResult reason) => reason switch
    {
        CoreResult.PairingRejected => Localization.Strings.PairingFailedRejected,
        CoreResult.PeerBusy => Localization.Strings.PairingFailedBusy,
        CoreResult.GroupFull => Localization.Strings.PairingFailedGroupFull,
        CoreResult.InAnotherGroup => Localization.Strings.PairingFailedInAnotherGroup,
        CoreResult.TimedOut => Localization.Strings.PairingFailedTimedOut,
        _ => Localization.Strings.PairingFailedUnreachable,
    };

    [RelayCommand]
    private Task AcceptAsync() => _owner.AnswerAsync(accepted: true);

    [RelayCommand]
    private Task RejectAsync() => _owner.AnswerAsync(accepted: false);

    [RelayCommand]
    private void Close() => _owner.ClosePairing();

    private string Format(string text) => string.Format(CultureInfo.CurrentCulture, text, PeerName);
}
