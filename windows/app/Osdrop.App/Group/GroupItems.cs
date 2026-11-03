using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Osdrop.App.Engine;
using Osdrop.App.Interop;
using Osdrop.Localization;

namespace Osdrop.App.Group;

/// <summary>A member as the group window and the mesh window show it.</summary>
public sealed partial class MemberItem : ObservableObject
{
    internal MemberItem(GroupMember member)
    {
        DeviceId = member.DeviceId;
        Name = member.Name;
        IsSelf = member.IsSelf;
        IsConnected = member.IsConnected;
        IsWindows = member.Platform == Platform.Windows;
    }

    public string DeviceId { get; }
    public string Name { get; }
    public bool IsSelf { get; }
    public bool IsConnected { get; }
    public bool IsWindows { get; }
    public bool CanRemove => !IsSelf;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Status))]
    public partial bool IsResponsive { get; set; } = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(RoundTrip), nameof(ClockOffset), nameof(Rejected))]
    internal partial SessionStats? Stats { get; set; }

    public string Status => IsSelf ? Strings.GroupThisComputer
        : !IsConnected ? Strings.GroupNotConnected
        : IsResponsive ? Strings.GroupConnected
        : Strings.GroupNotResponding;

    public string RoundTrip => Stats is null ? Strings.MeshNoSession
        : Format(Strings.MeshRoundTrip, (Stats.RoundTripMicroseconds / 1000.0).ToString("F1", CultureInfo.CurrentCulture));

    public string ClockOffset => Stats is null ? string.Empty : Format(Strings.MeshClockOffset, Stats.ClockOffsetMicroseconds);

    public string Rejected => Stats is null ? string.Empty : Format(Strings.MeshRejected, Stats.DatagramsRejected);

    private static string Format(string text, object value) => string.Format(CultureInfo.CurrentCulture, text, value);
}

/// <summary>A computer nearby that this PC can pair with.</summary>
public sealed class NearbyItem
{
    internal NearbyItem(NearbyComputer computer)
    {
        DeviceId = computer.DeviceId;
        Name = computer.Name;
        IsWindows = computer.Platform == Platform.Windows;
        Note = computer.NeedsUpdate ? Strings.GroupNeedsUpdate
            : computer.InAnotherGroup ? Strings.GroupInAnotherGroup
            : string.Empty;
        CanPair = !computer.NeedsUpdate;
    }

    public string DeviceId { get; }
    public string Name { get; }
    public bool IsWindows { get; }
    public string Note { get; }
    public bool HasNote => Note.Length > 0;
    public bool CanPair { get; }
}
