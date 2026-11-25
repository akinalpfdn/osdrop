using System.Collections.ObjectModel;
using System.Collections.Specialized;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Services;
using Osdrop.App.Transfers;

namespace Osdrop.App.Corners;

/// <summary>What a phone's box shows.</summary>
public enum CornerBoxMode
{
    /// <summary>Nothing shows; the corner still takes drags.</summary>
    Hidden,
    /// <summary>A drag is over the corner: the box shows where it would be dropped.</summary>
    Hovering,
    /// <summary>The box lists what went to the phone and what came from it since it opened.</summary>
    Open,
}

/// <summary>
/// One phone's box in its corner (DEVPLAN Phase 21; the Mac's CornerBox): it shows while a drag is over the
/// corner, and lists the transfers with the phone that started after it opened. Received files are dragged out
/// of it. A box that only sent files closes a moment after the last one ended.
/// </summary>
public sealed partial class CornerBoxViewModel : ObservableObject, IDisposable
{
    private readonly TransfersViewModel _transfers;
    private readonly Func<string> _name;
    private readonly Func<TimeSpan, CancellationToken, Task> _delay;
    /// <summary>Transfers listed before the box opened: not its own.</summary>
    private readonly HashSet<TransferKey> _before = [];
    private CancellationTokenSource? _linger;

    internal CornerBoxViewModel(
        string deviceId, Func<string> name, TransfersViewModel transfers,
        Func<TimeSpan, CancellationToken, Task>? delay = null)
    {
        DeviceId = deviceId;
        _name = name;
        _transfers = transfers;
        _delay = delay ?? Task.Delay;
        _transfers.Rows.CollectionChanged += OnRowsChanged;
        Rebuild();
    }

    /// <summary>The phone (hex device ID).</summary>
    public string DeviceId { get; }

    public string Name => _name();

    /// <summary>"Drop here to send to Galaxy S23."</summary>
    public string DropHint => LocalizedText.Format(Localization.Strings.CornerDropHere, Name);

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen), nameof(ShowsHint), nameof(ShowsRows))]
    public partial CornerBoxMode Mode { get; private set; }

    public bool IsOpen => Mode == CornerBoxMode.Open;

    /// <summary>The transfers with the phone since the box opened, as the transfers list orders them.</summary>
    public ObservableCollection<TransferRow> Rows { get; } = [];

    /// <summary>While a drag is over it, or before anything is listed: where to drop.</summary>
    public bool ShowsHint => Mode == CornerBoxMode.Hovering || Rows.Count == 0;

    public bool ShowsRows => !ShowsHint;

    /// <summary>A received file can be dragged out.</summary>
    public bool HasFiles => Rows.Any(row => row.HasFile);

    /// <summary>A drag that can be taken reached the corner.</summary>
    internal void Entered()
    {
        if (Mode == CornerBoxMode.Hidden)
        {
            Mode = CornerBoxMode.Hovering;
        }
    }

    /// <summary>The drag left the corner without a drop.</summary>
    internal void Left()
    {
        if (Mode == CornerBoxMode.Hovering)
        {
            Mode = CornerBoxMode.Hidden;
        }
    }

    /// <summary>
    /// Shows the box with the transfers that start from now on; <paramref name="including"/>, a transfer listed
    /// just before (the one that opened it), is its own too.
    /// </summary>
    internal void Open(TransferKey? including = null)
    {
        if (Mode != CornerBoxMode.Open)
        {
            _before.Clear();
            foreach (TransferRow row in _transfers.Rows.Where(row => row.DeviceId == DeviceId && row.Key != including))
            {
                _ = _before.Add(row.Key);
            }
            Mode = CornerBoxMode.Open;
        }
        Rebuild();
    }

    [RelayCommand]
    private void Close()
    {
        CancelLinger();
        Mode = CornerBoxMode.Hidden;
        _before.Clear();
        Rebuild();
    }

    [RelayCommand]
    private static void ShowInFolder(TransferRow? row)
    {
        if (row?.File is { } file)
        {
            Shell.ShowInFolder(file);
        }
    }

    public void Dispose()
    {
        _transfers.Rows.CollectionChanged -= OnRowsChanged;
        CancelLinger();
    }

    private void OnRowsChanged(object? sender, NotifyCollectionChangedEventArgs e) => Rebuild();

    private void Rebuild()
    {
        Rows.Clear();
        foreach (TransferRow row in _transfers.Rows.Where(row => row.DeviceId == DeviceId && !_before.Contains(row.Key)))
        {
            Rows.Add(row);
        }
        OnPropertyChanged(nameof(ShowsHint));
        OnPropertyChanged(nameof(ShowsRows));
        OnPropertyChanged(nameof(HasFiles));
        WatchLinger();
    }

    /// <summary>A box that only sent files closes a moment after the last one ended.</summary>
    private void WatchLinger()
    {
        CancelLinger();
        if (Mode != CornerBoxMode.Open || Rows.Count == 0 || Rows.Any(row => !row.IsSending || row.IsRunning))
        {
            return;
        }
        _linger = new CancellationTokenSource();
        _ = LingerAsync(_linger.Token);
    }

    private async Task LingerAsync(CancellationToken cancellation)
    {
        try
        {
            await _delay(AppTunables.CornerSentLinger, cancellation).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (!cancellation.IsCancellationRequested && Mode == CornerBoxMode.Open)
        {
            Close();
        }
    }

    private void CancelLinger()
    {
        _linger?.Cancel();
        _linger?.Dispose();
        _linger = null;
    }

    partial void OnModeChanged(CornerBoxMode value) => WatchLinger();
}
