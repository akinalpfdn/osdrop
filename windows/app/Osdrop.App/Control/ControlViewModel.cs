using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Services;
using Osdrop.App.Settings;

namespace Osdrop.App.Control;

/// <summary>
/// The cursor and keyboard across computers, as the panel shows them (the Mac's InputStatus,
/// QuickToggles and ControlStatus): where the mouse and keyboard are, who controls this PC, the
/// lock, why the cursor came back, and whether OsdropInput answers.
/// </summary>
public sealed partial class ControlViewModel : ObservableObject
{
    private const byte _refused = 4;
    private const byte _refusedLocked = 1;
    private const byte _refusedAsleep = 2;
    private const byte _refusedNotPermitted = 3;

    private readonly IGroupEngine _engine;
    private readonly GroupViewModel _group;
    private readonly AppSettings _settings;
    private readonly Func<Task<InputServiceState>> _queryService;
    private readonly Func<Task<bool>> _repairService;
    private InputState _state = InputState.Unknown;
    private bool _applyingState;

    internal ControlViewModel(
        IGroupEngine engine, GroupViewModel group, AppSettings settings,
        Func<Task<InputServiceState>>? queryService = null, Func<Task<bool>>? repairService = null)
    {
        _engine = engine;
        _group = group;
        _settings = settings;
        _settings.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(AppSettings.ReturnHotkey) or nameof(AppSettings.LockHotkey))
            {
                OnPropertyChanged(nameof(LockTitle));
                OnPropertyChanged(nameof(LockedText));
                OnPropertyChanged(nameof(HintText));
            }
        };
        _queryService = queryService ?? InputService.QueryAsync;
        _repairService = repairService ?? InputService.RepairAsync;
        _group.InputChanged += () => _ = RefreshAsync();
        _group.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(GroupViewModel.HasConnectedOthers))
            {
                Update(); // names and the hint follow the members
            }
        };
    }

    /// <summary>Another computer controls this PC: the tray icon changes.</summary>
    public event Action<bool>? ControlledChanged;

    /// <summary>"Keep the cursor where it is"; switching it here asks the engine.</summary>
    [ObservableProperty]
    public partial bool IsLocked { get; set; }

    [ObservableProperty]
    public partial bool IsControlled { get; private set; }

    /// <summary>Where the mouse and keyboard are, when it is worth a line.</summary>
    [ObservableProperty]
    public partial string? InputStatus { get; private set; }

    [ObservableProperty]
    public partial string? ControlledBy { get; private set; }

    /// <summary>Why the cursor came back; null when there is nothing to say.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasNotice))]
    public partial string? Notice { get; private set; }

    public bool HasNotice => Notice is not null;

    /// <summary>How to cross, while another member is connected and nothing else is said.</summary>
    [ObservableProperty]
    public partial bool ShowsHint { get; private set; }

    /// <summary>What keeps other computers from controlling this PC; null while OsdropInput answers.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasServiceProblem))]
    public partial string? ServiceProblem { get; private set; }

    public bool HasServiceProblem => ServiceProblem is not null;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RepairCommand))]
    public partial bool IsRepairing { get; private set; }

    public string LockTitle => LocalizedText.Format(Localization.Strings.PanelLockInput, HotkeyText.Display(_settings.LockHotkey));

    public string LockedText => LocalizedText.Format(Localization.Strings.ControlLocked, HotkeyText.Display(_settings.LockHotkey));

    public string HintText => LocalizedText.Format(Localization.Strings.ControlHint, HotkeyText.Display(_settings.ReturnHotkey));

    public async Task RefreshAsync()
    {
        InputState state = await _engine.GetInputStateAsync().ConfigureAwait(true);
        bool wasReachable = _state.IsReachable;
        _state = state;
        Update();
        if (!state.IsReachable)
        {
            await DescribeServiceAsync().ConfigureAwait(true);
        }
        else if (!wasReachable)
        {
            ServiceProblem = null;
        }
    }

    [RelayCommand]
    private Task DismissNoticeAsync()
    {
        Notice = null;
        return _engine.DismissInputNoticeAsync();
    }

    [RelayCommand(CanExecute = nameof(CanRepair))]
    private async Task RepairAsync()
    {
        IsRepairing = true;
        try
        {
            bool repaired = await _repairService().ConfigureAwait(true);
            if (!repaired)
            {
                ServiceProblem = Localization.Strings.ServiceRepairFailed;
            }
            // Once OsdropInput answers, the engine reports it and the problem goes.
        }
        finally
        {
            IsRepairing = false;
        }
    }

    private bool CanRepair() => !IsRepairing;

    partial void OnIsLockedChanged(bool value)
    {
        if (!_applyingState)
        {
            _ = _engine.LockInputAsync(value);
        }
    }

    private async Task DescribeServiceAsync()
    {
        InputServiceState service = await _queryService().ConfigureAwait(true);
        if (_state.IsReachable)
        {
            return; // it answered meanwhile
        }
        ServiceProblem = service switch
        {
            InputServiceState.NotInstalled => Localization.Strings.ServiceNotInstalled,
            InputServiceState.Stopped => Localization.Strings.ServiceStopped,
            _ => Localization.Strings.ServiceNotAnswering,
        };
    }

    private void Update()
    {
        _applyingState = true;
        IsLocked = _state.IsLocked;
        _applyingState = false;

        bool controlled = _state.ControllerId is not null;
        ControlledBy = controlled ? LocalizedText.Format(Localization.Strings.ControlControlledBy, _group.NameOf(_state.ControllerId!)) : null;
        InputStatus = _state.TargetId is string target
            ? LocalizedText.Format(Localization.Strings.PanelInputElsewhere, _group.NameOf(target))
            : controlled ? null : Localization.Strings.PanelInputHere;
        Notice = _state.Notice is CaptureNotice notice ? Describe(notice) : null;
        ShowsHint = _group.HasConnectedOthers && _state.IsReachable && !_state.IsLocked;
        if (IsControlled != controlled)
        {
            IsControlled = controlled;
            ControlledChanged?.Invoke(controlled);
        }
    }

    private string Describe(CaptureNotice notice)
    {
        string name = _group.NameOf(notice.DeviceId);
        string text = notice.Reason != _refused ? Localization.Strings.ControlLost
            : notice.Refused switch
            {
                _refusedLocked => Localization.Strings.ControlRefusedLocked,
                _refusedAsleep => Localization.Strings.ControlRefusedAsleep,
                _refusedNotPermitted => Localization.Strings.ControlRefusedNotPermitted,
                _ => Localization.Strings.ControlRefusedBusy,
            };
        return LocalizedText.Format(text, name);
    }
}
