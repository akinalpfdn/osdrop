using System.Windows;
using System.Windows.Interop;
using H.NotifyIcon;
using Microsoft.Win32;
using Osdrop.App.Control;
using Osdrop.App.Diagnostics;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Services;
using Osdrop.App.Settings;
using Osdrop.App.Tray;
using Osdrop.App.TrayPanel;
using Wpf.Ui.Appearance;

namespace Osdrop.App;

/// <summary>
/// Tray application: the engine runs from start to quit; a click on the tray icon opens the panel,
/// and Settings, pairing and copied files have their own windows.
/// </summary>
public partial class App : Application, IDisposable
{
    private TaskbarIcon? _trayIcon;
    private TrayIconTheme? _trayIconTheme;
    private SingleInstance? _instance;
    private AppSettings? _settings;
    private EngineHost? _engine;
    private GroupViewModel? _group;
    private PanelViewModel? _panelModel;
    private PanelWindow? _panel;
    private SettingsViewModel? _settingsModel;
    private SettingsWindow? _settingsWindow;
    private PairingWindowPresenter? _pairingWindow;
    private ClipboardFetchWindowPresenter? _clipboardFetchWindow;
    private MeshWindow? _meshWindow;
    private bool _quitting;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        _instance = SingleInstance.Claim();
        if (_instance is null)
        {
            Shutdown(); // the running Osdrop opens its panel instead
            return;
        }
        ApplySystemTheme();

        _settings = AppSettings.Load();
        var updates = new UpdatesViewModel(_settings);
        _panelModel = new PanelViewModel(updates, OpenSettings, OpenMesh, QuitAsync);
        _panel = new PanelWindow(_panelModel);
        // WPF makes the first window the main window, and every theme apply (also those the Settings
        // window's theme watcher makes) clears the main window's background for Mica. The panel has
        // no backdrop and would turn white, so it must not be the main window.
        MainWindow = null;
        _ = new WindowInteropHelper(_panel).EnsureHandle();
        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;

        _trayIcon = (TaskbarIcon)FindResource(TrayResourceKeys.TrayIcon);
        _trayIcon.DataContext = new TrayViewModel(_panel.Toggle, OpenSettings, QuitAsync);
        _trayIconTheme = new TrayIconTheme(_trayIcon);
        _trayIcon.ForceCreate(enablesEfficiencyMode: false);
        _instance.OnShowRequested(() => Dispatcher.BeginInvoke(ShowPanel));

        _ = StartAsync(updates);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        Dispose();
        base.OnExit(e);
    }

    public void Dispose()
    {
        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        SystemEvents.DisplaySettingsChanged -= OnDisplaySettingsChanged;
        _engine?.Shutdown();
        _engine = null;
        _trayIconTheme?.Dispose();
        _trayIconTheme = null;
        _trayIcon?.Dispose();
        _trayIcon = null;
        _instance?.Dispose();
        _instance = null;
        GC.SuppressFinalize(this);
    }

    private async Task StartAsync(UpdatesViewModel updates)
    {
        await StartEngineAsync().ConfigureAwait(true);
        await updates.CheckIfDueAsync().ConfigureAwait(true);
    }

    private async Task StartEngineAsync()
    {
        if (!await Native.IsAvailableAsync().ConfigureAwait(true))
        {
            Fail(Localization.Strings.EngineUnavailable);
            return;
        }
        (CoreResult result, EngineHost? engine) =
            await EngineHost.StartAsync(Dispatcher, _settings!.EngineOptions).ConfigureAwait(true);
        if (engine is null)
        {
            Fail(result == CoreResult.UnsupportedVersion ? Localization.Strings.EngineNewerVersion : Localization.Strings.EngineUnavailable);
            return;
        }
        Log.Write(LogLevel.Info, "The engine started");
        _engine = engine;
        _group = new GroupViewModel(engine);
        var control = new ControlViewModel(engine, _group, _settings);
        control.ControlledChanged += controlled =>
        {
            if (_trayIconTheme is not null)
            {
                _trayIconTheme.IsControlled = controlled;
            }
        };
        _settingsModel = new SettingsViewModel(
            _settings, engine, _group, control, _panelModel!.Updates, CoreVersionAsync, Clipboard.SetText);
        _pairingWindow = new PairingWindowPresenter(_group);
        _clipboardFetchWindow = new ClipboardFetchWindowPresenter(_group);
        _group.NotificationRequested += (title, message) => _trayIcon?.ShowNotification(title, message);
        SystemEvents.PowerModeChanged += OnPowerModeChanged;
        SystemEvents.DisplaySettingsChanged += OnDisplaySettingsChanged;
        _panelModel.Started(_group, control, _settingsModel);
        await SettingsViewModel.ApplyPointerSpeedsAsync(engine, _settings).ConfigureAwait(true);
        await _group.RefreshAsync().ConfigureAwait(true);
        await control.RefreshAsync().ConfigureAwait(true);
    }

    private void Fail(string message)
    {
        Log.Write(LogLevel.Error, $"The engine did not start: {message}");
        _panelModel!.StartFailure = message;
        ShowPanel(); // the user should see why nothing works
    }

    private static Task<string> CoreVersionAsync() => Native.Run(() =>
    {
        NativeVersion version = NativeMethods.CoreVersion();
        return $"{version.Major}.{version.Minor}.{version.Patch}";
    });

    private static void ApplySystemTheme()
    {
        ApplicationTheme theme = ApplicationThemeManager.GetSystemTheme() switch
        {
            SystemTheme.Dark or SystemTheme.CapturedMotion or SystemTheme.Glow => ApplicationTheme.Dark,
            SystemTheme.HCBlack or SystemTheme.HCWhite or SystemTheme.HC1 or SystemTheme.HC2 => ApplicationTheme.HighContrast,
            _ => ApplicationTheme.Light,
        };
        ApplicationThemeManager.Apply(theme, Wpf.Ui.Controls.WindowBackdropType.None, true);
    }

    // Raised on a system events thread; a light/dark switch arrives as General.
    private void OnUserPreferenceChanged(object? sender, UserPreferenceChangedEventArgs e)
    {
        if (e.Category == UserPreferenceCategory.General)
        {
            _ = Dispatcher.BeginInvoke(ApplySystemTheme);
        }
    }

    private void ShowPanel()
    {
        if (_panel is { IsVisible: false } && !_quitting)
        {
            _panel.Toggle();
        }
    }

    private void OpenSettings()
    {
        _panel?.Hide();
        if (_settingsModel is null)
        {
            ShowPanel(); // still starting, or it failed: the panel says which
            return;
        }
        if (_settingsWindow is null)
        {
            _settingsWindow = new SettingsWindow(_settingsModel);
            _settingsWindow.Closed += (_, _) => _settingsWindow = null;
            _settingsWindow.Show();
        }
        if (_settingsWindow.WindowState == WindowState.Minimized)
        {
            _settingsWindow.WindowState = WindowState.Normal;
        }
        _ = _settingsWindow.Activate();
    }

    private void OpenMesh()
    {
        if (_group is null)
        {
            return;
        }
        _panel?.Hide();
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

    /// <summary>Ends sessions first (other computers see this PC leave), then exits.</summary>
    private async Task QuitAsync()
    {
        if (_quitting)
        {
            return;
        }
        _quitting = true;
        _panel?.Hide();
        Dispose();
        await Log.FlushAsync().ConfigureAwait(true);
        if (_panel is not null)
        {
            _panel.IsQuitting = true;
        }
        Shutdown();
    }
}
