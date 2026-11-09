using CommunityToolkit.Mvvm.Input;

namespace Osdrop.App.Tray;

/// <summary>What the tray icon does: a click opens the panel; its menu has the quick actions.</summary>
public sealed partial class TrayViewModel(Action togglePanel, Action openSettings, Func<Task> quit)
{
    [RelayCommand]
    private void TogglePanel() => togglePanel();

    [RelayCommand]
    private void OpenSettings() => openSettings();

    [RelayCommand]
    private Task QuitAsync() => quit();
}
