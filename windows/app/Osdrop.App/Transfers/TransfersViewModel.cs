using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Services;

namespace Osdrop.App.Transfers;

/// <summary>Where a transfer of dragged files stands.</summary>
public enum TransferState
{
    Running,
    Done,
    Failed,
    Cancelled,
}

/// <summary>One item of a drag on its way, in either direction, as the panel lists it.</summary>
public sealed partial class TransferRow : ObservableObject
{
    private readonly TransfersViewModel _owner;

    internal TransferRow(
        TransfersViewModel owner, TransferKey key, string name, string peer, ulong bytesDone, ulong bytesTotal)
    {
        _owner = owner;
        Key = key;
        Name = name;
        Peer = peer;
        BytesDone = bytesDone;
        BytesTotal = bytesTotal;
    }

    internal TransferKey Key { get; }

    public string Name { get; }

    /// <summary>The other computer, by its name.</summary>
    public string Peer { get; }

    public bool IsSending => Key.IsSending;

    /// <summary>"To Mac" or "From Mac".</summary>
    public string Direction => Format(IsSending ? Localization.Strings.TransfersTo : Localization.Strings.TransfersFrom, Peer);

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Percent))]
    [NotifyPropertyChangedFor(nameof(Progress))]
    public partial ulong BytesDone { get; internal set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Percent))]
    [NotifyPropertyChangedFor(nameof(Progress))]
    public partial ulong BytesTotal { get; internal set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsRunning))]
    [NotifyPropertyChangedFor(nameof(Status))]
    [NotifyPropertyChangedFor(nameof(Summary))]
    [NotifyPropertyChangedFor(nameof(HasFailed))]
    [NotifyCanExecuteChangedFor(nameof(CancelCommand))]
    public partial TransferState State { get; internal set; }

    public bool IsRunning => State == TransferState.Running;

    public bool HasFailed => State == TransferState.Failed;

    public double Percent => BytesTotal == 0
        ? (State == TransferState.Done ? 100 : 0)
        : Math.Min(100.0 * BytesDone / BytesTotal, 100);

    /// <summary>"12 MB of 40 MB".</summary>
    public string Progress => Format(
        Localization.Strings.TransfersBytes, ClipboardFetchViewModel.Size(BytesDone), ClipboardFetchViewModel.Size(BytesTotal));

    /// <summary>How it ended; empty while it runs.</summary>
    public string Status => State switch
    {
        TransferState.Done => Localization.Strings.TransfersDone,
        TransferState.Failed => Localization.Strings.TransfersFailed,
        TransferState.Cancelled => Localization.Strings.TransfersCancelled,
        _ => string.Empty,
    };

    /// <summary>"From Mac · Done": one line once it ended, so a finished transfer takes little room.</summary>
    public string Summary => Format(Localization.Strings.TransfersStatus, Direction, Status);

    /// <summary>The links in a received item that did not arrive, known once it arrived.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(LinksNote))]
    [NotifyPropertyChangedFor(nameof(HasLinksNote))]
    public partial ulong LinksLeftOut { get; internal set; }

    /// <summary>"2 links inside did not arrive"; empty when none.</summary>
    public string LinksNote => LinksNoteFor(LinksLeftOut);

    public bool HasLinksNote => LinksLeftOut > 0;

    internal static string LinksNoteFor(ulong count) => count switch
    {
        0 => string.Empty,
        1 => Localization.Strings.TransfersLinkLeftOut,
        _ => Format(Localization.Strings.TransfersLinksLeftOut, count.ToString(CultureInfo.CurrentCulture)),
    };

    [RelayCommand(CanExecute = nameof(IsRunning))]
    private Task CancelAsync() => _owner.CancelAsync(this);

    private static string Format(string text, params string[] values) =>
        string.Format(CultureInfo.CurrentCulture, text, values);
}

/// <summary>
/// A transfer this PC receives (by its ID), or an item a member fetches: of this PC's drag, or of
/// files this PC sent a phone (<see cref="ToPhone"/>).
/// </summary>
internal readonly record struct TransferKey(bool IsSending, ulong Id, string DeviceId, bool ToPhone = false)
{
    public static TransferKey Receive(ulong transferId) => new(false, transferId, string.Empty);

    /// <summary><paramref name="item"/>: the drag's or the send's ID in the high 32 bits, the item's index in the low.</summary>
    public static TransferKey Send(ulong item, string deviceId, bool toPhone = false) => new(true, item, deviceId, toPhone);

    public uint DragId => (uint)(Id >> 32);

    public ulong SendId => Id >> 32;
}

/// <summary>
/// Transfers of dragged files in both directions, for the panel (DEVPLAN Phase 14, as the Mac's
/// menu has them): their progress, a way to cancel them while they run, and a notification when
/// one ends. Explorer also shows a received copy's progress.
/// </summary>
public sealed partial class TransfersViewModel : ObservableObject
{
    private readonly IGroupEngine _engine;
    private readonly Func<string, string> _nameOf;
    private readonly Action<string, string> _notify;
    private readonly HashSet<TransferKey> _cancelled = [];
    /// <summary>Sends to phones the user withdrew: their end needs no notification of its own.</summary>
    private readonly HashSet<ulong> _withdrawnSends = [];
    /// <summary>Ends of transfers reported before they were listed (clipboard ones never are).</summary>
    private readonly Dictionary<ulong, (CoreResult Result, ulong LinksLeftOut)> _early = [];

    internal TransfersViewModel(IGroupEngine engine, Func<string, string> nameOf, Action<string, string> notify)
    {
        _engine = engine;
        _nameOf = nameOf;
        _notify = notify;
        _engine.EventReceived += OnEvent;
    }

    /// <summary>Running transfers, then the latest that ended.</summary>
    public ObservableCollection<TransferRow> Rows { get; } = [];

    public bool HasRows => Rows.Count > 0;

    /// <summary>The newest rows, the ones the panel shows.</summary>
    public IReadOnlyList<TransferRow> PanelRows => [.. Rows.Take(AppTunables.TransfersPanelRows)];

    /// <summary>Rows the panel leaves to the transfers window.</summary>
    public bool HasMoreRows => Rows.Count > AppTunables.TransfersPanelRows;

    /// <summary>"Show all (8)".</summary>
    public string ShowAllText => LocalizedText.Format(Localization.Strings.TransfersShowAll, Rows.Count);

    /// <summary>The user asked for the transfers window.</summary>
    public event Action? ShowAllRequested;

    [RelayCommand]
    private void ShowAll() => ShowAllRequested?.Invoke();

    internal Task CancelAsync(TransferRow row)
    {
        if (!row.IsRunning)
        {
            return Task.CompletedTask;
        }
        _cancelled.Add(row.Key);
        return row.Key switch
        {
            { ToPhone: true } key => WithdrawSendAsync(key.SendId),
            { IsSending: true } key => _engine.WithdrawDragAsync(key.DragId),
            var key => _engine.CancelTransferAsync(key.Id),
        };
    }

    private Task WithdrawSendAsync(ulong sendId)
    {
        _ = _withdrawnSends.Add(sendId);
        return _engine.WithdrawSendAsync(sendId);
    }

    private void OnEvent(EngineEvent received)
    {
        switch (received.Kind)
        {
            case EngineEventKind.TransferReceiving:
                Receiving(received);
                break;
            case EngineEventKind.TransferProgress:
                Update(TransferKey.Receive(received.Connection), received.BytesDone, received.BytesTotal);
                break;
            case EngineEventKind.TransferFinished:
                Finished(received.Connection, received.Reason, received.Code);
                break;
            case EngineEventKind.SendProgress:
                Sent(received, toPhone: false);
                break;
            case EngineEventKind.SentProgress:
                Sent(received, toPhone: true);
                break;
            case EngineEventKind.SendFinished:
                SendFinished(received);
                break;
            default:
                break;
        }
    }

    private void Receiving(EngineEvent received)
    {
        var key = TransferKey.Receive(received.Connection);
        Insert(new TransferRow(this, key, received.Name, _nameOf(received.DeviceId), 0, received.BytesTotal));
        if (_early.Remove(received.Connection, out var ended))
        {
            Finished(received.Connection, ended.Result, ended.LinksLeftOut);
        }
    }

    private void Finished(ulong transferId, CoreResult result, ulong linksLeftOut)
    {
        var key = TransferKey.Receive(transferId);
        if (Find(key) is not { } row)
        {
            _early[transferId] = (result, linksLeftOut);
            if (_early.Count > AppTunables.TransfersEarlyEnds)
            {
                _early.Remove(_early.Keys.Min());
            }
            return;
        }
        TransferState state = result switch
        {
            CoreResult.Ok => TransferState.Done,
            CoreResult.Cancelled when _cancelled.Contains(key) => TransferState.Cancelled,
            _ => TransferState.Failed,
        };
        if (state == TransferState.Done && row.IsRunning)
        {
            row.LinksLeftOut = linksLeftOut;
        }
        End(key, state);
    }

    private void Sent(EngineEvent received, bool toPhone)
    {
        const uint sending = 1;
        const uint done = 2;
        var key = TransferKey.Send(received.Connection, received.DeviceId, toPhone);
        if (Find(key) is null)
        {
            if (received.Code != sending)
            {
                return; // ended before it was listed
            }
            string name = received.Name.Length > 0 ? received.Name : Localization.Strings.TransfersCopiedFiles;
            Insert(new TransferRow(this, key, name, _nameOf(received.DeviceId), received.BytesDone, received.BytesTotal));
        }
        Update(key, received.BytesDone, received.BytesTotal);
        if (received.Code == done)
        {
            End(key, TransferState.Done);
        }
        else if (received.Code != sending)
        {
            End(key, _cancelled.Contains(key) ? TransferState.Cancelled : TransferState.Failed);
        }
    }

    /// <summary>
    /// A phone answered a send, or it ended: when the phone did not take it, its rows still running
    /// fail, and a send that listed none is reported on its own.
    /// </summary>
    private void SendFinished(EngineEvent received)
    {
        const uint receivedByPhone = 1;
        bool withdrawn = _withdrawnSends.Remove(received.Connection);
        if (received.Code == receivedByPhone)
        {
            return; // its rows end as their items arrive
        }
        TransferKey[] running = [.. Rows
            .Where(row => row.IsRunning && row.Key.ToPhone && row.Key.SendId == received.Connection)
            .Select(row => row.Key)];
        foreach (TransferKey key in running)
        {
            End(key, _cancelled.Contains(key) ? TransferState.Cancelled : TransferState.Failed);
        }
        if (running.Length == 0 && !withdrawn)
        {
            _notify(Localization.Strings.TransfersNotifyNotSent, _nameOf(received.DeviceId));
        }
    }

    private TransferRow? Find(TransferKey key) => Rows.FirstOrDefault(row => row.Key == key);

    /// <summary>Above the rows that ended.</summary>
    private void Insert(TransferRow row)
    {
        int index = 0;
        while (index < Rows.Count && Rows[index].IsRunning)
        {
            index++;
        }
        Rows.Insert(index, row);
        RowsChanged();
    }

    private void RowsChanged()
    {
        OnPropertyChanged(nameof(HasRows));
        OnPropertyChanged(nameof(PanelRows));
        OnPropertyChanged(nameof(HasMoreRows));
        OnPropertyChanged(nameof(ShowAllText));
    }

    private void Update(TransferKey key, ulong bytesDone, ulong bytesTotal)
    {
        if (Find(key) is { IsRunning: true } row)
        {
            row.BytesDone = bytesDone;
            row.BytesTotal = bytesTotal;
        }
    }

    private void End(TransferKey key, TransferState state)
    {
        if (Find(key) is not { IsRunning: true } row)
        {
            return;
        }
        Rows.Remove(row);
        row.State = state;
        if (state == TransferState.Done)
        {
            row.BytesDone = row.BytesTotal;
        }
        _cancelled.Remove(key);
        Insert(row);
        while (Rows.Count(ended => !ended.IsRunning) > AppTunables.TransfersEndedRows)
        {
            Rows.Remove(Rows.Last(ended => !ended.IsRunning));
        }
        RowsChanged();
        Notify(row);
    }

    private void Notify(TransferRow row)
    {
        string body = string.Format(
            CultureInfo.CurrentCulture, row.IsSending ? Localization.Strings.TransfersToItem : Localization.Strings.TransfersFromItem,
            row.Name, row.Peer);
        if (row.HasLinksNote)
        {
            body += Environment.NewLine + row.LinksNote;
        }
        string? title = (row.IsSending, row.State) switch
        {
            (false, TransferState.Done) => Localization.Strings.TransfersNotifyReceived,
            (false, TransferState.Failed) => Localization.Strings.TransfersNotifyNotReceived,
            (true, TransferState.Done) => Localization.Strings.TransfersNotifySent,
            (true, TransferState.Failed) => Localization.Strings.TransfersNotifyNotSent,
            _ => null,
        };
        if (title is not null)
        {
            _notify(title, body);
        }
    }
}
