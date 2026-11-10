using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;

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

    [RelayCommand(CanExecute = nameof(IsRunning))]
    private Task CancelAsync() => _owner.CancelAsync(this);

    private static string Format(string text, params string[] values) =>
        string.Format(CultureInfo.CurrentCulture, text, values);
}

/// <summary>A transfer this PC receives (by its ID), or an item of this PC's drag a member fetches.</summary>
internal readonly record struct TransferKey(bool IsSending, ulong Id, string DeviceId)
{
    public static TransferKey Receive(ulong transferId) => new(false, transferId, string.Empty);

    /// <summary><paramref name="item"/>: the drag's ID in the high 32 bits, the item's index in the low.</summary>
    public static TransferKey Send(ulong item, string deviceId) => new(true, item, deviceId);

    public uint DragId => (uint)(Id >> 32);
}

/// <summary>
/// Transfers of dragged files in both directions, for the panel (DEVPLAN Phase 14, as the Mac's
/// menu has them): their progress, a way to cancel them while they run, and a notification when
/// one ends. Explorer also shows a received copy's progress.
/// </summary>
public sealed class TransfersViewModel : ObservableObject
{
    private readonly IGroupEngine _engine;
    private readonly Func<string, string> _nameOf;
    private readonly Action<string, string> _notify;
    private readonly HashSet<TransferKey> _cancelled = [];
    /// <summary>Ends of transfers reported before they were listed (clipboard ones never are).</summary>
    private readonly Dictionary<ulong, CoreResult> _early = [];

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

    internal Task CancelAsync(TransferRow row)
    {
        if (!row.IsRunning)
        {
            return Task.CompletedTask;
        }
        _cancelled.Add(row.Key);
        return row.IsSending ? _engine.WithdrawDragAsync(row.Key.DragId) : _engine.CancelTransferAsync(row.Key.Id);
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
                Finished(received.Connection, received.Reason);
                break;
            case EngineEventKind.SendProgress:
                Sent(received);
                break;
            default:
                break;
        }
    }

    private void Receiving(EngineEvent received)
    {
        var key = TransferKey.Receive(received.Connection);
        Insert(new TransferRow(this, key, received.Name, _nameOf(received.DeviceId), 0, received.BytesTotal));
        if (_early.Remove(received.Connection, out CoreResult result))
        {
            Finished(received.Connection, result);
        }
    }

    private void Finished(ulong transferId, CoreResult result)
    {
        var key = TransferKey.Receive(transferId);
        if (Find(key) is null)
        {
            _early[transferId] = result;
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
        End(key, state);
    }

    private void Sent(EngineEvent received)
    {
        const uint sending = 1;
        const uint done = 2;
        var key = TransferKey.Send(received.Connection, received.DeviceId);
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
        OnPropertyChanged(nameof(HasRows));
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
        OnPropertyChanged(nameof(HasRows));
        Notify(row);
    }

    private void Notify(TransferRow row)
    {
        string body = string.Format(
            CultureInfo.CurrentCulture, row.IsSending ? Localization.Strings.TransfersToItem : Localization.Strings.TransfersFromItem,
            row.Name, row.Peer);
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
