using System.Windows;
using H.NotifyIcon;
using Microsoft.Win32;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Tray;
using Osdrop.Localization;

namespace Osdrop.App;

/// <summary>Tray application: the engine runs from start to quit; windows open from the tray.</summary>
public partial class App : Application
{
    private TaskbarIcon? _trayIcon;
    private EngineHost? _engine;
    private GroupViewModel? _group;
    private PairingWindowPresenter? _pairingWindow;
    private GroupWindow? _groupWindow;
    private MeshWindow? _meshWindow;
    private string? _startFailure;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        _trayIcon = (TaskbarIcon)FindResource(TrayResourceKeys.TrayIcon);
        _trayIcon.DataContext = new TrayViewModel(OpenComputers, OpenMesh, Shutdown);
        _trayIcon.ForceCreate(enablesEfficiencyMode: false);
        _ = StartEngineAsync();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        SystemEvents.DisplaySettingsChanged -= OnDisplaySettingsChanged;
        _engine?.Shutdown();
        _trayIcon?.Dispose();
        base.OnExit(e);
    }

    private async Task StartEngineAsync()
    {
        if (!await Native.IsAvailableAsync().ConfigureAwait(true))
        {
            _startFailure = Strings.EngineUnavailable;
            return;
        }
        (CoreResult result, EngineHost? engine) = await EngineHost.StartAsync(Dispatcher).ConfigureAwait(true);
        if (engine is null)
        {
            _startFailure = result == CoreResult.UnsupportedVersion ? Strings.EngineNewerVersion : Strings.EngineUnavailable;
            return;
        }
        _engine = engine;
        _group = new GroupViewModel(engine);
        _pairingWindow = new PairingWindowPresenter(_group);
        SystemEvents.PowerModeChanged += OnPowerModeChanged;
        SystemEvents.DisplaySettingsChanged += OnDisplaySettingsChanged;
        await _group.RefreshAsync().ConfigureAwait(true);
    }

    private void OpenComputers()
    {
        if (_group is null)
        {
            _ = MessageBox.Show(_startFailure ?? Strings.EngineStarting, Strings.GroupWindowTitle);
            return;
        }
        if (_groupWindow is null)
        {
            _groupWindow = new GroupWindow(_group);
            _groupWindow.Closed += (_, _) => _groupWindow = null;
        }
        _groupWindow.Show();
        _ = _groupWindow.Activate();
    }

    private void OpenMesh()
    {
        if (_group is null)
        {
            return;
        }
        if (_meshWindow is null)
        {
            _meshWindow = new MeshWindow(_group);
            _meshWindow.Closed += (_, _) => _meshWindow = null;
        }
        _meshWindow.Show();
        _ = _meshWindow.Activate();
    }

    private void OnPowerModeChanged(object? sender, PowerModeChangedEventArgs e)
    {
        if (_engine is null)
        {
            return;
        }
        if (e.Mode == PowerModes.Suspend)
        {
            _ = _engine.CloseAllAsync(sleeping: true);
        }
        else if (e.Mode == PowerModes.Resume)
        {
            _ = _engine.ResumeAsync();
        }
    }

    private void OnDisplaySettingsChanged(object? sender, EventArgs e) => _ = _engine?.ScreensChangedAsync();
}
