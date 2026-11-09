using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Settings;

namespace Osdrop.App.Services;

/// <summary>Update checks: once a day at launch when enabled, and on request.</summary>
public sealed partial class UpdatesViewModel(AppSettings settings) : ObservableObject
{
    private Uri _page = AppTunables.ReleasesPageUri;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsUpdateAvailable))]
    public partial string? AvailableVersionMessage { get; set; }

    [ObservableProperty]
    public partial string CheckResult { get; set; } = string.Empty;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(CheckNowCommand))]
    public partial bool IsChecking { get; set; }

    public bool IsUpdateAvailable => AvailableVersionMessage is not null;

    public string CurrentVersionText { get; } =
        LocalizedText.Format(Localization.Strings.PanelVersion, UpdateChecker.CurrentVersion.ToString(3));

    /// <summary>The daily automatic check; does nothing when turned off or done recently.</summary>
    public async Task CheckIfDueAsync()
    {
        DateTimeOffset? last = settings.LastUpdateCheck;
        if (!settings.ChecksForUpdates || (last is not null && DateTimeOffset.UtcNow - last < AppTunables.UpdateCheckInterval))
        {
            return;
        }
        await CheckAsync().ConfigureAwait(true);
    }

    [RelayCommand(CanExecute = nameof(CanCheck))]
    private Task CheckNowAsync() => CheckAsync();

    [RelayCommand]
    private void OpenDownload() => Shell.Open(_page);

    private bool CanCheck() => !IsChecking;

    private async Task CheckAsync()
    {
        IsChecking = true;
        CheckResult = Localization.Strings.UpdatesChecking;
        try
        {
            (UpdateCheckOutcome outcome, AvailableUpdate? update) = await UpdateChecker.CheckAsync().ConfigureAwait(true);
            switch (outcome)
            {
                case UpdateCheckOutcome.UpdateAvailable when update is not null:
                    _page = update.Page;
                    AvailableVersionMessage = LocalizedText.Format(
                        Localization.Strings.UpdatesAvailable, update.Version.ToString(3));
                    CheckResult = AvailableVersionMessage;
                    settings.LastUpdateCheck = DateTimeOffset.UtcNow;
                    break;
                case UpdateCheckOutcome.UpToDate:
                    AvailableVersionMessage = null;
                    CheckResult = Localization.Strings.UpdatesUpToDate;
                    settings.LastUpdateCheck = DateTimeOffset.UtcNow;
                    break;
                default:
                    CheckResult = Localization.Strings.UpdatesFailed;
                    break;
            }
        }
        finally
        {
            IsChecking = false;
        }
    }
}
