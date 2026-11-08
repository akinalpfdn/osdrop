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

    private static string Formatted(string text, string value) =>
        string.Format(System.Globalization.CultureInfo.CurrentCulture, text, value);

    [Fact]
    public async Task MembersAndNearbyComputersFollowTheEngine()
    {
        var engine = new FakeEngine { Info = new EngineInfo(_self, "47474747474747474747474747474747", 50_000) };
        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false));
        engine.Members.Add(new GroupMember(_laptop, "Laptop", Platform.MacOS, IsSelf: false, IsConnected: true));
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
    public void CodesKeepLeadingZeros() => Assert.Equal("000 042", PairingViewModel.Display(42));

    private sealed class FakeEngine : IGroupEngine
    {
        public event Action<EngineEvent>? EventReceived;

        public EngineInfo? Info { get; set; }
        public List<GroupMember> Members { get; } = [];
        public List<NearbyComputer> Nearby { get; } = [];
        public List<string> Paired { get; } = [];
        public List<(ulong Connection, bool Accepted)> Answers { get; } = [];
        public List<string> Revoked { get; } = [];

        public void Raise(EngineEvent received) => EventReceived?.Invoke(received);

        public Task<EngineInfo?> GetInfoAsync() => Task.FromResult(Info);
        public Task<IReadOnlyList<GroupMember>> GetMembersAsync() => Task.FromResult<IReadOnlyList<GroupMember>>([.. Members]);
        public Task<IReadOnlyList<NearbyComputer>> GetNearbyAsync() => Task.FromResult<IReadOnlyList<NearbyComputer>>([.. Nearby]);

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

        public Task<CoreResult> LeaveGroupAsync() => Task.FromResult(CoreResult.Ok);
    }
}
