using System.Globalization;
using Osdrop.App.Interop;

namespace Osdrop.App.Services;

/// <summary>Formats localized strings with the user's culture.</summary>
internal static class LocalizedText
{
    public static string Format(string text, params object[] values) =>
        string.Format(CultureInfo.CurrentCulture, text, values);

    /// <summary>A localized, actionable message for every error core reports (as the Mac's ErrorMessages).</summary>
    public static string Error(CoreResult result) => result switch
    {
        CoreResult.UnsupportedVersion => Localization.Strings.ErrorUnsupportedVersion,
        CoreResult.TooLarge => Localization.Strings.ErrorTooLarge,
        CoreResult.DecryptionFailed or CoreResult.HandshakeOutOfOrder or CoreResult.Replayed
            or CoreResult.NonceExhausted or CoreResult.InvalidKey => Localization.Strings.ErrorSecure,
        CoreResult.TimedOut => Localization.Strings.ErrorTimedOut,
        CoreResult.ConnectionClosed or CoreResult.Network => Localization.Strings.ErrorNetwork,
        CoreResult.PeerError or CoreResult.UnexpectedMessage => Localization.Strings.ErrorPeer,
        CoreResult.NotTrusted => Localization.Strings.ErrorNotTrusted,
        CoreResult.PairingRejected => Localization.Strings.ErrorPairingRejected,
        CoreResult.PeerBusy => Localization.Strings.ErrorPeerBusy,
        CoreResult.PeerSleeping => Localization.Strings.ErrorPeerSleeping,
        CoreResult.NotPermitted => Localization.Strings.ErrorNotPermitted,
        CoreResult.GroupFull => Localization.Strings.ErrorGroupFull,
        CoreResult.InAnotherGroup => Localization.Strings.ErrorInAnotherGroup,
        CoreResult.InvalidLayout => Localization.Strings.ErrorInvalidLayout,
        CoreResult.Cancelled => Localization.Strings.ErrorCancelled,
        CoreResult.IoFailure => Localization.Strings.ErrorIoFailure,
        CoreResult.NoSpace => Localization.Strings.ErrorNoSpace,
        CoreResult.IntegrityFailed => Localization.Strings.ErrorIntegrityFailed,
        CoreResult.NotFound => Localization.Strings.ErrorNotFound,
        _ => Localization.Strings.ErrorInternal,
    };
}
