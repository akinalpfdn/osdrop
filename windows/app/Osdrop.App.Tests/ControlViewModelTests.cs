using Osdrop.App.Control;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Services;
using Xunit;

namespace Osdrop.App.Tests;

/// <summary>The panel's input lines with a fake engine: where the input is, control, the lock and the service.</summary>
public sealed class ControlViewModelTests
{
    private const string _self = "30303030303030303030303030303030";
    private const string _laptop = "31313131313131313131313131313131";

    private static async Task<(FakeEngine Engine, GroupViewModel Group, ControlViewModel Control)> MakeAsync(
        InputServiceState service = InputServiceState.Running)
    {
        var engine = new FakeEngine();
        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false, FakeEngine.AllAllowed));
        engine.Members.Add(new GroupMember(_laptop, "Laptop", Platform.MacOS, IsSelf: false, IsConnected: true, FakeEngine.AllAllowed));
        var group = new GroupViewModel(engine);
        await group.RefreshAsync();
        var control = new ControlViewModel(engine, group, () => Task.FromResult(service), () => Task.FromResult(true));
        return (engine, group, control);
    }

    [Fact]
    public async Task TheInputLineSaysWhereTheMouseAndKeyboardAre()
    {
        (FakeEngine engine, _, ControlViewModel control) = await MakeAsync();
        await control.RefreshAsync();
        Assert.Equal(Localization.Strings.PanelInputHere, control.InputStatus);
        Assert.True(control.ShowsHint);

        engine.InputState = new InputState(IsReachable: true, IsLocked: false, ControllerId: null, TargetId: _laptop, Notice: null);
        await control.RefreshAsync();
        Assert.Equal(LocalizedText.Format(Localization.Strings.PanelInputElsewhere, "Laptop"), control.InputStatus);
    }

    [Fact]
    public async Task BeingControlledChangesTheTrayIconAndSaysByWhom()
    {
        (FakeEngine engine, _, ControlViewModel control) = await MakeAsync();
        var changes = new List<bool>();
        control.ControlledChanged += changes.Add;
        engine.InputState = new InputState(IsReachable: true, IsLocked: false, ControllerId: _laptop, TargetId: null, Notice: null);
        engine.Raise(new EngineEvent(EngineEventKind.InputChanged));
        await Task.Yield();
        Assert.True(control.IsControlled);
        Assert.Equal(LocalizedText.Format(Localization.Strings.ControlControlledBy, "Laptop"), control.ControlledBy);
        Assert.Null(control.InputStatus);
        Assert.Equal([true], changes);
    }

    [Fact]
    public async Task ARefusedCrossingLeavesANoticeUntilDismissed()
    {
        (FakeEngine engine, _, ControlViewModel control) = await MakeAsync();
        engine.InputState = new InputState(true, false, null, null, new CaptureNotice(Reason: 4, Refused: 2, _laptop));
        await control.RefreshAsync();
        Assert.Equal(LocalizedText.Format(Localization.Strings.ControlRefusedAsleep, "Laptop"), control.Notice);

        await control.DismissNoticeCommand.ExecuteAsync(null);
        Assert.False(control.HasNotice);
        Assert.Equal(1, engine.NoticeDismissals);

        engine.InputState = new InputState(true, false, null, null, new CaptureNotice(Reason: 6, Refused: 0, _laptop));
        await control.RefreshAsync();
        Assert.Equal(LocalizedText.Format(Localization.Strings.ControlLost, "Laptop"), control.Notice);
    }

    [Fact]
    public async Task TheLockSwitchAsksTheEngineButTheEnginesStateDoesNotEchoBack()
    {
        (FakeEngine engine, _, ControlViewModel control) = await MakeAsync();
        control.IsLocked = true;
        Assert.Equal([true], engine.Locks);

        engine.InputState = new InputState(true, IsLocked: false, null, null, null); // the hotkey unlocked it
        await control.RefreshAsync();
        Assert.False(control.IsLocked);
        Assert.Equal([true], engine.Locks);
    }

    [Fact]
    public async Task AStoppedServiceIsNamedWithItsRepair()
    {
        (FakeEngine engine, _, ControlViewModel control) = await MakeAsync(InputServiceState.Stopped);
        engine.InputState = new InputState(IsReachable: false, false, null, null, null);
        await control.RefreshAsync();
        Assert.Equal(Localization.Strings.ServiceStopped, control.ServiceProblem);
        Assert.False(control.ShowsHint);

        engine.InputState = InputState.Unknown;
        await control.RefreshAsync();
        Assert.False(control.HasServiceProblem);
    }
}
