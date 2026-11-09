using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Xunit;

namespace Osdrop.App.Tests;

/// <summary>The group on this PC with a fake engine: pairings, members and what the windows show.</summary>
public sealed class GroupViewModelTests
{
    private const string _laptop = "31313131313131313131313131313131";
    private const string _self = "30303030303030303030303030303030";
    private const string _other = "32323232323232323232323232323232";

    [Fact]
    public async Task APairingShowsTheCodeSendsTheAnswerAndEndsWhenTheComputerConnects()
    {
        var engine = new FakeEngine();
        engine.Nearby.Add(new NearbyComputer(_laptop, "Laptop", Platform.MacOS, false, false));
        var group = new GroupViewModel(engine);
        await group.RefreshAsync();
        NearbyItem laptop = Assert.Single(group.Nearby);

        await group.PairCommand.ExecuteAsync(laptop);
        Assert.Equal(PairingStage.Dialing, group.Pairing?.Stage);
        Assert.Equal([_laptop], engine.Paired);

        engine.Raise(new EngineEvent(EngineEventKind.PairingCode, Code: 42_917, Connection: 5, DeviceId: _laptop, Name: "Laptop"));
        Assert.Equal(PairingStage.Confirm, group.Pairing?.Stage);
        Assert.Equal("042 917", group.Pairing?.CodeText);
        Assert.True(group.Pairing?.CanAnswer);

        await group.Pairing!.AcceptCommand.ExecuteAsync(null);
        Assert.Equal([(5UL, true)], engine.Answers);
        Assert.Equal(PairingStage.Waiting, group.Pairing?.Stage);

        engine.Raise(new EngineEvent(EngineEventKind.PeerConnected, DeviceId: _laptop, Name: "Laptop"));
        Assert.Null(group.Pairing);
    }

    [Fact]
    public async Task ARefusedPairingSaysWhyAndClosingAStillShownCodeRejectsIt()
    {
        var engine = new FakeEngine();
        var group = new GroupViewModel(engine);
        engine.Raise(new EngineEvent(EngineEventKind.PairingCode, Code: 7, Connection: 9, DeviceId: _laptop, Name: "Laptop"));
        group.ClosePairing();
        Assert.Null(group.Pairing);
        await Task.Yield();
        Assert.Equal([(9UL, false)], engine.Answers);

        engine.Raise(new EngineEvent(EngineEventKind.PairingCode, Code: 8, Connection: 10, DeviceId: _laptop, Name: "Laptop"));
        engine.Raise(new EngineEvent(EngineEventKind.PairingFailed, Reason: CoreResult.InAnotherGroup, Connection: 10));
        Assert.Equal(PairingStage.Failed, group.Pairing?.Stage);
        Assert.Equal(Localization.Strings.PairingFailedInAnotherGroup, group.Pairing?.Detail);
        Assert.True(group.Pairing?.CanClose);
    }

    [Fact]
    public void ACopyThatDoesNotArriveLeavesANoticeUntilDismissed()
    {
        var engine = new FakeEngine();
        var group = new GroupViewModel(engine);
        Assert.False(group.HasClipboardNotice);

        engine.Raise(new EngineEvent(EngineEventKind.ClipboardUnavailable, DeviceId: _laptop, Name: "Laptop"));
        Assert.True(group.HasClipboardNotice);
        Assert.Equal(Formatted(Localization.Strings.ClipboardUnavailable, "Laptop"), group.ClipboardNotice);
        group.DismissClipboardNoticeCommand.Execute(null);
        Assert.False(group.HasClipboardNotice);

        engine.Raise(new EngineEvent(EngineEventKind.ClipboardTooLarge, DeviceId: _laptop, Name: ""));
        Assert.Equal(
            Formatted(Localization.Strings.ClipboardTooLarge, Localization.Strings.ClipboardOtherComputer),
            group.ClipboardNotice);
    }

    [Fact]
    public void CopiedFilesOnTheirWayShowTheirProgressUntilTheyEnd()
    {
        var engine = new FakeEngine();
        var group = new GroupViewModel(engine);
        var notifications = new List<(string Title, string Text)>();
        group.NotificationRequested += (title, text) => notifications.Add((title, text));

        engine.Raise(new EngineEvent(
            EngineEventKind.ClipboardFetching, DeviceId: _laptop, Name: "Laptop", BytesDone: 512, BytesTotal: 2048));
        Assert.NotNull(group.ClipboardFetch);
        Assert.Equal(25, group.ClipboardFetch.Percent);
        Assert.Equal(Formatted(Localization.Strings.ClipboardFetching, "Laptop"), group.ClipboardFetch.Message);

        engine.Raise(new EngineEvent(EngineEventKind.ClipboardFetchEnded));
        Assert.Null(group.ClipboardFetch);
        engine.Raise(new EngineEvent(EngineEventKind.ClipboardReady, DeviceId: _laptop, Name: "Laptop"));
        Assert.Equal([(Localization.Strings.ClipboardReadyTitle, Formatted(Localization.Strings.ClipboardReady, "Laptop"))], notifications);

        engine.Raise(new EngineEvent(EngineEventKind.ClipboardTooLarge, DeviceId: _laptop, Name: "Laptop"));
        Assert.Equal(2, notifications.Count);
        Assert.Equal(Localization.Strings.ClipboardNoticeTitle, notifications[1].Title);
    }

    [Fact]
    public void CancellingCopiedFilesStopsThem()
    {
        var engine = new FakeEngine();
        var group = new GroupViewModel(engine);
        engine.Raise(new EngineEvent(EngineEventKind.ClipboardFetching, Name: "Laptop", BytesDone: 0, BytesTotal: 10));
        group.ClipboardFetch!.CancelCommand.Execute(null);
        Assert.Null(group.ClipboardFetch);
        Assert.Equal(1, engine.FetchCancels);
    }

    [Fact]
    public void SizesReadAsExplorerShowsThem()
    {
        Assert.Equal(Formatted(Localization.Strings.UnitBytes, "512"), ClipboardFetchViewModel.Size(512));
        Assert.Equal(Formatted(Localization.Strings.UnitMegabytes, (1.5).ToString("0.#", System.Globalization.CultureInfo.CurrentCulture)),
            ClipboardFetchViewModel.Size(1_572_864));
    }

    private static string Formatted(string text, string value) =>
        string.Format(System.Globalization.CultureInfo.CurrentCulture, text, value);

    [Fact]
    public async Task MembersAndNearbyComputersFollowTheEngine()
    {
        var engine = new FakeEngine { Info = new EngineInfo(_self, "47474747474747474747474747474747", 50_000) };
        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false, FakeEngine.AllAllowed));
        engine.Members.Add(new GroupMember(_laptop, "Laptop", Platform.MacOS, IsSelf: false, IsConnected: true, FakeEngine.AllAllowed));
        var group = new GroupViewModel(engine);
        await group.RefreshAsync();
        Assert.True(group.IsInGroup);
        Assert.Equal(50_000, group.ListenPort);
        Assert.Equal(2, group.Members.Count);
        Assert.False(group.Members[0].CanRemove);
        Assert.Equal(Localization.Strings.GroupConnected, group.Members[1].Status);

        engine.Raise(new EngineEvent(EngineEventKind.PeerResponsive, DeviceId: _laptop, Responsive: false));
        Assert.Equal(Localization.Strings.GroupNotResponding, group.Members[1].Status);

        await group.RevokeCommand.ExecuteAsync(group.Members[1]);
        Assert.Equal([_laptop], engine.Revoked);

        engine.Raise(new EngineEvent(EngineEventKind.LeftGroup, Reason: CoreResult.NotTrusted));
        Assert.True(group.WasRemoved);
    }

    [Fact]
    public async Task AComputerIsBlockedFromItsCodeOrTheMemberListAndUnblocked()
    {
        var engine = new FakeEngine();
        var group = new GroupViewModel(engine);
        engine.Raise(new EngineEvent(EngineEventKind.PairingCode, Code: 7, Connection: 9, DeviceId: _laptop, Name: "Laptop"));
        await group.Pairing!.BlockCommand.ExecuteAsync(null);
        Assert.Null(group.Pairing);
        Assert.Equal([_laptop], engine.BlockedIds);
        Assert.Empty(engine.Answers);

        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false, FakeEngine.AllAllowed));
        engine.Members.Add(new GroupMember(_other, "Other PC", Platform.Windows, IsSelf: false, IsConnected: true, FakeEngine.AllAllowed));
        await group.RefreshAsync();
        await group.BlockCommand.ExecuteAsync(group.Members[0]);
        Assert.Equal([_laptop], engine.BlockedIds);
        await group.BlockCommand.ExecuteAsync(group.Members[1]);
        Assert.Equal([_laptop, _other], engine.BlockedIds);

        engine.Blocked.Add(new BlockedComputer(_laptop, "Laptop", Platform.MacOS));
        engine.Raise(new EngineEvent(EngineEventKind.GroupChanged));
        await Task.Yield();
        Assert.True(group.HasBlocked);
        BlockedItem blocked = Assert.Single(group.Blocked);
        await group.UnblockCommand.ExecuteAsync(blocked);
        Assert.Equal([_laptop], engine.Unblocked);

        engine.Raise(new EngineEvent(EngineEventKind.PairingCode, Code: 8, Connection: 10, DeviceId: _laptop, Name: "Laptop"));
        engine.Raise(new EngineEvent(EngineEventKind.PairingFailed, Reason: CoreResult.NotTrusted, Connection: 10));
        Assert.Equal(Localization.Strings.PairingFailedRefused, group.Pairing?.Detail);
    }

    [Fact]
    public void CodesKeepLeadingZeros() => Assert.Equal("000 042", PairingViewModel.Display(42));

    [Fact]
    public async Task APermissionSwitchedInSettingsReachesTheEngine()
    {
        var engine = new FakeEngine();
        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false, FakeEngine.AllAllowed));
        engine.Members.Add(new GroupMember(_laptop, "Laptop", Platform.MacOS, IsSelf: false, IsConnected: true, FakeEngine.AllAllowed));
        var group = new GroupViewModel(engine);
        await group.RefreshAsync();
        Assert.Empty(engine.PermissionChanges);
        MemberItem laptop = Assert.Single(group.Others);
        Assert.True(laptop.AllowsFiles);

        laptop.AllowsFiles = false;
        Assert.Equal([(_laptop, new Permissions(Control: true, Files: false, Clipboard: true))], engine.PermissionChanges);
        Assert.Equal("Laptop", group.NameOf(_laptop));
        Assert.Equal(Localization.Strings.ClipboardOtherComputer, group.NameOf(_other));
        Assert.True(group.HasConnectedOthers);
    }
}
