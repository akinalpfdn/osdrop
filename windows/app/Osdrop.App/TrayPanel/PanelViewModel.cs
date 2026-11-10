using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Control;
using Osdrop.App.Group;
using Osdrop.App.Services;
using Osdrop.App.Settings;
using Osdrop.App.Transfers;

namespace Osdrop.App.TrayPanel;

/// <summary>
/// The tray panel (the Mac's menu): where the input is, quick toggles, copied files on their way,
/// the group, and the way to Settings. It exists from the start; the engine's parts arrive once it runs.
/// </summary>
public sealed partial class PanelViewModel(
    UpdatesViewModel updates, Action openSettings, Action openMesh, Func<Task> quit) : ObservableObject
{
    public UpdatesViewModel Updates { get; } = updates;

    public string VersionText { get; } = updates.CurrentVersionText;

    /// <summary>The mesh window is a developer tool.</summary>
    public static bool ShowsMesh =>
#if DEBUG
        true;
#else
        false;
#endif

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsRunning))]
    [NotifyCanExecuteChangedFor(nameof(OpenSettingsCommand))]
    public partial GroupViewModel? Group { get; private set; }

    [ObservableProperty]
    public partial ControlViewModel? Control { get; private set; }

    [ObservableProperty]
    public partial SettingsViewModel? Settings { get; private set; }

    /// <summary>Dragged files on their way, in both directions.</summary>
    [ObservableProperty]
    public partial TransfersViewModel? Transfers { get; private set; }

    /// <summary>Why the engine did not start; null while it starts or runs.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStartFailure))]
    public partial string? StartFailure { get; set; }

    public bool HasStartFailure => StartFailure is not null;

    public bool IsRunning => Group is not null;

    private Func<bool, Task>? _setDragLoopback;

    /// <summary>Developer tool (debug builds): the drag bridge's loopback.</summary>
    [ObservableProperty]
    public partial bool IsDragLoopback { get; set; }

    internal void Started(
        GroupViewModel group, ControlViewModel control, SettingsViewModel settings, TransfersViewModel transfers,
        Func<bool, Task> setDragLoopback)
    {
        _setDragLoopback = setDragLoopback;
        Transfers = transfers;
        Control = control;
        Settings = settings;
        Group = group;
    }

    partial void OnIsDragLoopbackChanged(bool value) => _ = _setDragLoopback?.Invoke(value);

    [RelayCommand(CanExecute = nameof(IsRunning))]
    private void OpenSettings() => openSettings();

    [RelayCommand]
    private void OpenMesh() => openMesh();

    [RelayCommand]
    private Task QuitAsync() => quit();
}
