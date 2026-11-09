using System.Collections.ObjectModel;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Arrangement;
using Osdrop.App.Control;
using Osdrop.App.Diagnostics;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Services;

namespace Osdrop.App.Settings;

/// <summary>One choice of a picker: its value and what the picker shows.</summary>
public sealed record Choice<T>(T Value, string Name);

/// <summary>The pointer speed on one member, as a share of this PC's own (1 is the same).</summary>
public sealed partial class PointerSpeedRow : ObservableObject
{
    private readonly Action<PointerSpeedRow> _changed;

    internal PointerSpeedRow(string deviceId, string name, double speed, Action<PointerSpeedRow> changed)
    {
        DeviceId = deviceId;
        Name = name;
        Speed = speed;
        _changed = changed; // set last: loading the speed is no change
    }

    public string DeviceId { get; }
    public string Name { get; }

    [ObservableProperty]
    public partial double Speed { get; set; }

    partial void OnSpeedChanged(double value) => _changed?.Invoke(this);
}

/// <summary>
/// The Settings window (the Mac's SettingsViewModel): General, Arrangement, Devices, Input,
/// Clipboard and Files, Advanced. Every change is saved at once and reaches the engine at once.
/// </summary>
public sealed partial class SettingsViewModel : ObservableObject
{
    /// <summary>Enter modifier bits for each key, left and right (as the Mac's CrossingModifier).</summary>
    private const byte _control = 0b0001_0001;
    private const byte _shift = 0b0010_0010;
    private const byte _alt = 0b0100_0100;
    private const byte _windows = 0b1000_1000;

    private readonly IGroupEngine _engine;
    private readonly Func<Task<string>> _coreVersion;
    private readonly Action<string> _copy;
    private IReadOnlyList<LayoutComputer> _layout = [];

    internal SettingsViewModel(
        AppSettings settings, IGroupEngine engine, GroupViewModel group, ControlViewModel control,
        UpdatesViewModel updates, Func<Task<string>> coreVersion, Action<string> copy)
    {
        Settings = settings;
        _engine = engine;
        Group = group;
        Control = control;
        Updates = updates;
        _coreVersion = coreVersion;
        _copy = copy;
        StartsWithWindows = StartupRegistration.IsEnabled;
        group.ArrangementChanged += () => _ = RefreshArrangementAsync();
        group.Members.CollectionChanged += (_, _) => OnMembersChanged();
    }

    public AppSettings Settings { get; }
    public GroupViewModel Group { get; }
    public ControlViewModel Control { get; }
    public UpdatesViewModel Updates { get; }

    // --- General ---

    [ObservableProperty]
    public partial bool StartsWithWindows { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStartupError))]
    public partial string? StartupError { get; private set; }

    public bool HasStartupError => StartupError is not null;

    // --- Arrangement ---

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasArrangement))]
    public partial ArrangementGeometry? Arrangement { get; private set; }

    public bool HasArrangement => Arrangement is { Computers.Count: > 1 };

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasArrangementProblem))]
    public partial string? ArrangementProblem { get; private set; }

    public bool HasArrangementProblem => ArrangementProblem is not null;

    // --- Devices ---

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveNameCommand))]
    public partial string DeviceNameText { get; set; } = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasNameProblem))]
    public partial string? NameProblem { get; private set; }

    public bool HasNameProblem => NameProblem is not null;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveNameCommand))]
    public partial bool IsRenaming { get; private set; }

    // --- Input ---

    public IReadOnlyList<Choice<byte>> CrossingModifiers { get; } =
    [
        new(0, Localization.Strings.SettingsModifierNone),
        new(_control, Localization.Strings.SettingsModifierControl),
        new(_alt, Localization.Strings.SettingsModifierAlt),
        new(_windows, Localization.Strings.SettingsModifierWindows),
        new(_shift, Localization.Strings.SettingsModifierShift),
    ];

    public IReadOnlyList<Choice<ModifierMapping>> Mappings { get; } =
    [
        new(ModifierMapping.Shortcuts, Localization.Strings.SettingsMappingShortcuts),
        new(ModifierMapping.Positions, Localization.Strings.SettingsMappingPositions),
    ];

    public Choice<byte> CrossingModifier
    {
        get => CrossingModifiers.FirstOrDefault(choice => choice.Value == Settings.CrossingModifiers) ?? CrossingModifiers[0];
        set
        {
            if (value is not null && value.Value != Settings.CrossingModifiers)
            {
                Settings.CrossingModifiers = value.Value;
                ApplyInput();
            }
        }
    }

    public Choice<ModifierMapping> Mapping
    {
        get => Mappings.First(choice => choice.Value == Settings.ModifierMapping);
        set
        {
            if (value is not null && value.Value != Settings.ModifierMapping)
            {
                Settings.ModifierMapping = value.Value;
                ApplyInput();
            }
        }
    }

    public bool CrossesImmediately
    {
        get => Settings.CrossesImmediately;
        set
        {
            if (value != Settings.CrossesImmediately)
            {
                Settings.CrossesImmediately = value;
                OnPropertyChanged();
                ApplyInput();
            }
        }
    }

    public static double MinPushSeconds => AppTunables.MinPushSeconds;
    public static double MaxPushSeconds => AppTunables.MaxPushSeconds;
    public static double PushSecondsStep => AppTunables.PushSecondsStep;

    /// <summary>How long a push must last, in seconds.</summary>
    public double PushSeconds
    {
        get => Settings.PushMilliseconds / 1000.0;
        set
        {
            uint milliseconds = (uint)Math.Round(Math.Clamp(value, MinPushSeconds, MaxPushSeconds) * 1000);
            if (milliseconds != Settings.PushMilliseconds)
            {
                Settings.PushMilliseconds = milliseconds;
                OnPropertyChanged();
                OnPropertyChanged(nameof(PushText));
                ApplyInput();
            }
        }
    }

    public string PushText => LocalizedText.Format(Localization.Strings.SettingsPush, PushSeconds.ToString("0.0", System.Globalization.CultureInfo.CurrentCulture));

    public static double MinPointerSpeed => AppTunables.MinPointerSpeed;
    public static double MaxPointerSpeed => AppTunables.MaxPointerSpeed;

    public ObservableCollection<PointerSpeedRow> PointerSpeeds { get; } = [];

    /// <summary>The chord that brings the cursor home; a refused one is put back.</summary>
    public Hotkey ReturnHotkey
    {
        get => Settings.ReturnHotkey;
        set => _ = SetHotkeysAsync(value, Settings.LockHotkey);
    }

    public Hotkey LockHotkey
    {
        get => Settings.LockHotkey;
        set => _ = SetHotkeysAsync(Settings.ReturnHotkey, value);
    }

    /// <summary>Why the last chord was refused; null when it was taken.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasHotkeyMessage))]
    public partial string? HotkeyMessage { get; private set; }

    public bool HasHotkeyMessage => HotkeyMessage is not null;

    // --- Clipboard and files ---

    public IReadOnlyList<Choice<ulong>> FilesLimits { get; } =
        [.. AppTunables.ClipboardFilesLimits.Select(limit => new Choice<ulong>(limit, ClipboardFetchViewModel.Size(limit)))];

    public bool SharesClipboard
    {
        get => Settings.SharesClipboard;
        set
        {
            if (value != Settings.SharesClipboard)
            {
                Settings.SharesClipboard = value;
                OnPropertyChanged();
                ApplyClipboard();
            }
        }
    }

    public Choice<ulong> FilesLimit
    {
        get => FilesLimits.FirstOrDefault(choice => choice.Value == Settings.ClipboardFilesLimit) ?? FilesLimits[1];
        set
        {
            if (value is not null && value.Value != Settings.ClipboardFilesLimit)
            {
                Settings.ClipboardFilesLimit = value.Value;
                ApplyClipboard();
            }
        }
    }

    // --- Advanced ---

    [ObservableProperty]
    public partial bool DiagnosticsCopied { get; private set; }

    /// <summary>The window opened: fresh values for what changes outside it.</summary>
    public void Appeared()
    {
        StartsWithWindows = StartupRegistration.IsEnabled;
        DiagnosticsCopied = false;
        NameProblem = null;
        DeviceNameText = Group.Members.FirstOrDefault(member => member.IsSelf)?.Name ?? Settings.DeviceName ?? Environment.MachineName;
        OnMembersChanged();
        _ = RefreshArrangementAsync();
    }

    /// <summary>Every member's pointer speed, after the engine started or restarted.</summary>
    internal static async Task ApplyPointerSpeedsAsync(IGroupEngine engine, AppSettings settings)
    {
        foreach ((string deviceId, uint permille) in settings.PointerSpeeds)
        {
            _ = await engine.SetPointerSpeedAsync(deviceId, permille).ConfigureAwait(true);
        }
    }

    public async Task RefreshArrangementAsync()
    {
        _layout = await _engine.GetLayoutAsync().ConfigureAwait(true);
        Arrangement = new ArrangementGeometry(_layout, Group.Members);
    }

    partial void OnStartsWithWindowsChanged(bool value)
    {
        if (value == StartupRegistration.IsEnabled)
        {
            return;
        }
        StartupError = StartupRegistration.SetEnabled(value) ? null : Localization.Strings.SettingsStartWithWindowsFailed;
        StartsWithWindows = StartupRegistration.IsEnabled;
    }

    /// <summary>A computer dropped in the editor: it snaps beside the nearest other one, and every member gets the arrangement.</summary>
    [RelayCommand]
    private async Task MoveAsync(ArrangementMove? move)
    {
        if (move is null || Arrangement is null)
        {
            return;
        }
        var origin = Arrangement.Snapped(move.DeviceId, move.Origin);
        CoreResult result = await _engine.SetLayoutAsync(
            ArrangementGeometry.Placements(_layout, move.DeviceId, origin)).ConfigureAwait(true);
        ArrangementProblem = result == CoreResult.Ok ? null : LocalizedText.Error(result);
        await RefreshArrangementAsync().ConfigureAwait(true);
    }

    /// <summary>The name other computers show; every session restarts with it (as on the Mac).</summary>
    [RelayCommand(CanExecute = nameof(CanSaveName))]
    private async Task SaveNameAsync()
    {
        string? chosen = Clean(DeviceNameText);
        if (chosen == Settings.DeviceName)
        {
            return;
        }
        IsRenaming = true;
        try
        {
            Settings.DeviceName = chosen;
            CoreResult result = await _engine.RestartAsync(Settings.EngineOptions).ConfigureAwait(true);
            NameProblem = result == CoreResult.Ok ? null : LocalizedText.Error(result);
            await ApplyPointerSpeedsAsync(_engine, Settings).ConfigureAwait(true);
            await Group.RefreshAsync().ConfigureAwait(true);
            await Control.RefreshAsync().ConfigureAwait(true);
            await RefreshArrangementAsync().ConfigureAwait(true);
        }
        finally
        {
            IsRenaming = false;
        }
    }

    private bool CanSaveName() => !IsRenaming;

    [RelayCommand]
    private Task ResetHotkeysAsync() => SetHotkeysAsync(Hotkey.DefaultReturnHome, Hotkey.DefaultLockInput);

    /// <summary>Both chords, checked by the engine as the hook matches them; saved once taken.</summary>
    internal async Task SetHotkeysAsync(Hotkey returnHome, Hotkey lockInput)
    {
        CoreResult result = returnHome == lockInput || !returnHome.HasModifier || !lockInput.HasModifier
            ? CoreResult.InvalidArgument
            : await _engine.SetHotkeysAsync(returnHome, lockInput).ConfigureAwait(true);
        if (result == CoreResult.Ok)
        {
            Settings.ReturnHotkey = returnHome;
            Settings.LockHotkey = lockInput;
            HotkeyMessage = null;
        }
        else
        {
            HotkeyMessage = returnHome == lockInput ? Localization.Strings.HotkeyTaken
                : !returnHome.HasModifier || !lockInput.HasModifier ? Localization.Strings.HotkeyNeedsModifier
                : Localization.Strings.HotkeyInvalid;
        }
        OnPropertyChanged(nameof(ReturnHotkey)); // the recorders show what is in use
        OnPropertyChanged(nameof(LockHotkey));
    }

    [RelayCommand]
    private async Task CopyDiagnosticsAsync()
    {
        string coreVersion = await _coreVersion().ConfigureAwait(true);
        var report = new DiagnosticsReport(
            UpdateChecker.CurrentVersion.ToString(3), coreVersion, Environment.OSVersion.VersionString,
            DiagnosticFacts(), DiagnosticsReport.RecentLogLines(AppTunables.DiagnosticsLogLines),
            [DeviceNameText, .. Group.Members.Select(member => member.Name)]);
        _copy(report.Text);
        DiagnosticsCopied = true;
    }

    [RelayCommand]
    private static void OpenLogs() => Shell.OpenFolder(Log.Folder);

    [RelayCommand]
    private static void OpenServiceLogs() => Shell.OpenFolder(Log.ServiceFolder);

    /// <summary>Up to 64 bytes of UTF-8, cut at a character; empty: this PC's own name.</summary>
    internal static string? Clean(string text)
    {
        string trimmed = text.Trim();
        if (trimmed.Length == 0)
        {
            return null;
        }
        var cut = new StringBuilder();
        int bytes = 0;
        foreach (System.Text.Rune rune in trimmed.EnumerateRunes())
        {
            bytes += rune.Utf8SequenceLength;
            if (bytes > AppTunables.MaxDeviceNameBytes)
            {
                break;
            }
            _ = cut.Append(rune.ToString());
        }
        return cut.ToString().Trim();
    }

    private void OnMembersChanged()
    {
        PointerSpeeds.Clear();
        foreach (MemberItem member in Group.Others)
        {
            PointerSpeeds.Add(new PointerSpeedRow(
                member.DeviceId, member.Name, Settings.PointerSpeed(member.DeviceId) / 1000.0, OnSpeedChanged));
        }
        if (Arrangement is not null)
        {
            Arrangement = new ArrangementGeometry(_layout, Group.Members); // names and states follow
        }
    }

    private void OnSpeedChanged(PointerSpeedRow row)
    {
        uint permille = (uint)Math.Round(Math.Clamp(row.Speed, MinPointerSpeed, MaxPointerSpeed) * 1000);
        Settings.SetPointerSpeed(row.DeviceId, permille);
        _ = _engine.SetPointerSpeedAsync(row.DeviceId, permille);
    }

    private void ApplyInput() => _ = _engine.SetInputSettingsAsync(Settings.Input);

    private void ApplyClipboard() => _ = _engine.SetClipboardAsync(Settings.SharesClipboard, Settings.ClipboardFilesLimit);

    /// <summary>Settings and state, without names, keys or addresses.</summary>
    private List<(string Name, string Value)> DiagnosticFacts() =>
    [
        ("Members", $"{Group.Members.Count} ({Group.Members.Count(member => member.IsConnected)} connected)"),
        ("Input service", Control.HasServiceProblem ? "not answering" : "answering"),
        ("Controlled", Control.IsControlled.ToString()),
        ("Locked", Control.IsLocked.ToString()),
        ("Modifier mapping", Settings.ModifierMapping.ToString()),
        ("Push", $"{Settings.PushMilliseconds} ms, immediate {Settings.CrossesImmediately}, modifiers {Settings.CrossingModifiers}"),
        ("Shares clipboard", Settings.SharesClipboard.ToString()),
        ("Clipboard files limit", Settings.ClipboardFilesLimit.ToString(System.Globalization.CultureInfo.InvariantCulture)),
        ("Windows", Environment.OSVersion.Version.ToString()),
    ];
}
