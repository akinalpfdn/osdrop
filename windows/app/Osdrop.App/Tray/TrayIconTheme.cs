using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows;
using H.NotifyIcon;
using Microsoft.Win32;

namespace Osdrop.App.Tray;

/// <summary>
/// Keeps the tray icon readable: a dark glyph on a light taskbar, a light one on a dark taskbar;
/// filled while another computer controls this PC (the Mac swaps its menu bar icon too).
/// </summary>
internal sealed partial class TrayIconTheme : IDisposable
{
    private const string _personalizeKey = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";
    private const string _lightTaskbarValue = "SystemUsesLightTheme";
    private const int _smallIconWidthMetric = 49; // SM_CXSMICON

    private readonly TaskbarIcon _trayIcon;
    private Icon? _icon;
    private bool _controlled;

    public TrayIconTheme(TaskbarIcon trayIcon)
    {
        _trayIcon = trayIcon;
        Apply();
        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
    }

    /// <summary>Another computer controls this PC now, or no longer does. On the UI thread.</summary>
    public bool IsControlled
    {
        get => _controlled;
        set
        {
            if (_controlled != value)
            {
                _controlled = value;
                Apply();
            }
        }
    }

    public void Dispose()
    {
        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        _icon?.Dispose();
        _icon = null;
    }

    // Raised on a system events thread; the taskbar theme switch arrives as General.
    private void OnUserPreferenceChanged(object? sender, UserPreferenceChangedEventArgs e)
    {
        if (e.Category == UserPreferenceCategory.General)
        {
            _ = _trayIcon.Dispatcher.BeginInvoke(Apply);
        }
    }

    private void Apply()
    {
        bool light = TaskbarUsesLightTheme();
        string key = (_controlled, light) switch
        {
            (true, true) => TrayResourceKeys.ControlledIconForLightTaskbar,
            (true, false) => TrayResourceKeys.ControlledIconForDarkTaskbar,
            (false, true) => TrayResourceKeys.IconForLightTaskbar,
            (false, false) => TrayResourceKeys.IconForDarkTaskbar,
        };
        var uri = new Uri((string)Application.Current.FindResource(key));
        using var stream = Application.GetResourceStream(uri).Stream;
        int size = GetSystemMetrics(_smallIconWidthMetric);
        var icon = new Icon(stream, size, size);
        _trayIcon.Icon = icon;
        _icon?.Dispose();
        _icon = icon;
    }

    private static bool TaskbarUsesLightTheme()
    {
        using RegistryKey? personalize = Registry.CurrentUser.OpenSubKey(_personalizeKey);
        return personalize?.GetValue(_lightTaskbarValue) is int value && value != 0;
    }

    [LibraryImport("user32.dll")]
    private static partial int GetSystemMetrics(int index);
}
