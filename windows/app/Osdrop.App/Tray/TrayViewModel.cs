using CommunityToolkit.Mvvm.Input;

namespace Osdrop.App.Tray;

/// <summary>Commands behind the tray icon's context menu.</summary>
public sealed partial class TrayViewModel(Action shutdown)
{
    private readonly Action _shutdown = shutdown;

    [RelayCommand]
    private void Quit() => _shutdown();
}
