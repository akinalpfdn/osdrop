using System.Windows;
using H.NotifyIcon;
using Osdrop.App.Tray;

namespace Osdrop.App;

/// <summary>Tray-only application: no main window until the shell UI lands.</summary>
public partial class App : Application
{
    private TaskbarIcon? _trayIcon;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        _trayIcon = (TaskbarIcon)FindResource(TrayResourceKeys.TrayIcon);
        _trayIcon.DataContext = new TrayViewModel(Shutdown);
        _trayIcon.ForceCreate(enablesEfficiencyMode: false);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _trayIcon?.Dispose();
        base.OnExit(e);
    }
}
