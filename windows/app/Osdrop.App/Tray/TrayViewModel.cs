using CommunityToolkit.Mvvm.Input;

namespace Osdrop.App.Tray;

/// <summary>Commands behind the tray icon's context menu.</summary>
public sealed partial class TrayViewModel(Action openComputers, Action openMesh, Action shutdown)
{
    private readonly Action _openComputers = openComputers;
    private readonly Action _openMesh = openMesh;
    private readonly Action _shutdown = shutdown;

    /// <summary>The mesh window is a developer tool.</summary>
    public static bool ShowsMesh =>
#if DEBUG
        true;
#else
        false;
#endif

    [RelayCommand]
    private void OpenComputers() => _openComputers();

    [RelayCommand]
    private void OpenMesh() => _openMesh();

    [RelayCommand]
    private void Quit() => _shutdown();
}
