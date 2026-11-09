using System.IO;
using System.Windows;
using Osdrop.App.Arrangement;
using Osdrop.App.Control;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Services;
using Osdrop.App.Settings;
using Xunit;

namespace Osdrop.App.Tests;

/// <summary>Settings: what is saved, what reaches the engine, and the arrangement editor's snapping.</summary>
public sealed class SettingsTests
{
    private const string _self = "30303030303030303030303030303030";
    private const string _laptop = "31313131313131313131313131313131";
    private const int _sixteenths = 16;

    private static ArrangementComputer Computer(string id, double x, double y, double width = 1920, double height = 1080) =>
        new(id, id, IsThisPc: false, IsConnected: true, new Point(x * _sixteenths, y * _sixteenths),
            [new Rect(0, 0, width * _sixteenths, height * _sixteenths)]);

    [Fact]
    public void ADroppedComputerSnapsBesideTheNearestEdgeSharingHalfOfIt()
    {
        var geometry = new ArrangementGeometry([Computer(_self, 0, 0), Computer(_laptop, 2500, 900, 1440, 900)]);
        Point snapped = geometry.Snapped(_laptop, new Point(2000 * _sixteenths, 900 * _sixteenths));
        Assert.Equal(1920 * _sixteenths, snapped.X); // to the right of the PC
        Assert.Equal((1080 - 450) * _sixteenths, snapped.Y); // at least half of its 900 shared
    }

    [Fact]
    public void AComputerAloneStaysWhereItIsDropped()
    {
        var geometry = new ArrangementGeometry([Computer(_self, 0, 0)]);
        Assert.Equal(new Point(5, 7), geometry.Snapped(_self, new Point(5, 7)));
    }

    [Fact]
    public async Task MovingAComputerSendsEveryPlacementAndExplainsARefusal()
    {
        var engine = new FakeEngine();
        engine.Members.Add(new GroupMember(_self, "PC", Platform.Windows, IsSelf: true, IsConnected: false, FakeEngine.AllAllowed));
        engine.Members.Add(new GroupMember(_laptop, "Laptop", Platform.MacOS, IsSelf: false, IsConnected: true, FakeEngine.AllAllowed));
        engine.Layout.Add(new LayoutComputer(_self, new Placement(_self, 0, 0, false), [new MonitorFrame(0, 0, 1920 * _sixteenths, 1080 * _sixteenths)]));
        engine.Layout.Add(new LayoutComputer(_laptop, new Placement(_laptop, 1920 * _sixteenths, 0, false), [new MonitorFrame(0, 0, 1440 * _sixteenths, 900 * _sixteenths)]));
        (SettingsViewModel settings, _) = await MakeAsync(engine);
        await settings.RefreshArrangementAsync();
        Assert.True(settings.HasArrangement);

        await settings.MoveCommand.ExecuteAsync(new ArrangementMove(_laptop, new Point(-1500 * _sixteenths, 100 * _sixteenths)));
        IReadOnlyList<Placement> sent = Assert.Single(engine.Arrangements);
        Assert.Equal(2, sent.Count);
        Placement laptop = Assert.Single(sent, placement => placement.DeviceId == _laptop);
        Assert.Equal(-1440 * _sixteenths, laptop.X); // to the PC's left
        Assert.False(settings.HasArrangementProblem);

        engine.ArrangementResult = CoreResult.InvalidLayout;
        await settings.MoveCommand.ExecuteAsync(new ArrangementMove(_laptop, new Point(0, 0)));
        Assert.Equal(Localization.Strings.ErrorInvalidLayout, settings.ArrangementProblem);
    }

    [Fact]
    public async Task InputAndClipboardChangesAreSavedAndReachTheEngine()
    {
        var engine = new FakeEngine();
        (SettingsViewModel settings, AppSettings stored) = await MakeAsync(engine);

        settings.CrossesImmediately = true;
        settings.Mapping = settings.Mappings.Single(choice => choice.Value == ModifierMapping.Positions);
        settings.PushSeconds = 1.5;
        Assert.Equal(new InputSettings(ModifierMapping.Positions, Immediate: true, 0, 1500), engine.InputSettings[^1]);

        settings.SharesClipboard = false;
        settings.FilesLimit = settings.FilesLimits[^1];
        Assert.Equal((false, 4096UL * 1024 * 1024), engine.ClipboardSettings[^1]);
        Assert.False(stored.SharesClipboard);
    }

    [Fact]
    public async Task ANewNameRestartsTheEngineWithIt()
    {
        var engine = new FakeEngine();
        (SettingsViewModel settings, AppSettings stored) = await MakeAsync(engine);
        settings.DeviceNameText = "  Studio PC  ";
        await settings.SaveNameCommand.ExecuteAsync(null);
        Assert.Equal("Studio PC", stored.DeviceName);
        Assert.Equal("Studio PC", Assert.Single(engine.Restarts).DeviceName);

        await settings.SaveNameCommand.ExecuteAsync(null);
        Assert.Single(engine.Restarts); // the same name again
    }

    [Fact]
    public async Task ChordsAreCheckedByTheEngineAndKeptOnlyWhenTaken()
    {
        var engine = new FakeEngine();
        (SettingsViewModel settings, AppSettings stored) = await MakeAsync(engine);
        var lockOnL = new Hotkey(Hotkey.Control | Hotkey.Alt, 0x26, false);
        settings.LockHotkey = lockOnL;
        await Task.Yield();
        Assert.Equal((Hotkey.DefaultReturnHome, lockOnL), Assert.Single(engine.Hotkeys));
        Assert.Equal(lockOnL, stored.LockHotkey);
        Assert.False(settings.HasHotkeyMessage);

        await settings.SetHotkeysAsync(lockOnL, lockOnL);
        Assert.Equal(Localization.Strings.HotkeyTaken, settings.HotkeyMessage);
        Assert.Single(engine.Hotkeys); // not even asked

        engine.HotkeyResult = CoreResult.InvalidArgument;
        await settings.SetHotkeysAsync(new Hotkey(Hotkey.Control, 0x7F, true), lockOnL);
        Assert.Equal(Localization.Strings.HotkeyInvalid, settings.HotkeyMessage);
        Assert.Equal(Hotkey.DefaultReturnHome, stored.ReturnHotkey);

        engine.HotkeyResult = CoreResult.Ok;
        await settings.ResetHotkeysCommand.ExecuteAsync(null);
        Assert.Equal(Hotkey.DefaultLockInput, stored.LockHotkey);
    }

    [Fact]
    public void NamesAreCutAt64BytesOnACharacter()
    {
        string name = new string('a', 63) + "ğğ";
        Assert.Equal(new string('a', 63), SettingsViewModel.Clean(name));
        Assert.Null(SettingsViewModel.Clean("   "));
    }

    [Fact]
    public void SettingsSurviveARestartAndDamagedValuesFallBack()
    {
        string folder = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string path = Path.Combine(folder, "settings.json");
        try
        {
            var first = AppSettings.Load(path);
            first.DeviceName = "Studio PC";
            first.ModifierMapping = ModifierMapping.Positions;
            first.SetPointerSpeed(_laptop, 2000);
            var second = AppSettings.Load(path);
            Assert.Equal("Studio PC", second.DeviceName);
            Assert.Equal(ModifierMapping.Positions, second.ModifierMapping);
            Assert.Equal(2000U, second.PointerSpeed(_laptop));

            File.WriteAllText(path, """{"ClipboardFilesLimit": 7, "PushMilliseconds": 99999, "PointerSpeeds": {"x": 9}}""");
            var damaged = AppSettings.Load(path);
            Assert.Equal(AppTunables.DefaultClipboardFilesLimit, damaged.ClipboardFilesLimit);
            Assert.Equal(3000U, damaged.PushMilliseconds);
            Assert.Empty(damaged.PointerSpeeds);
        }
        finally
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        }
    }

    private static async Task<(SettingsViewModel Settings, AppSettings Stored)> MakeAsync(FakeEngine engine)
    {
        var group = new GroupViewModel(engine);
        await group.RefreshAsync();
        var stored = AppSettings.InMemory();
        var control = new ControlViewModel(
            engine, group, stored, () => Task.FromResult(InputServiceState.Running), () => Task.FromResult(true));
        var settings = new SettingsViewModel(
            stored, engine, group, control, new UpdatesViewModel(stored), () => Task.FromResult("0.0.0"), _ => { });
        return (settings, stored);
    }
}
