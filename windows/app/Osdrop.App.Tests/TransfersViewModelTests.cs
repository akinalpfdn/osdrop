using Osdrop.App.Engine;
using Osdrop.App.Interop;
using Osdrop.App.Transfers;
using Xunit;

namespace Osdrop.App.Tests;

/// <summary>Dragged files on their way in the panel, with a fake engine: progress, ends, cancel and notifications.</summary>
public sealed class TransfersViewModelTests
{
    private const string _laptop = "31313131313131313131313131313131";

    private static (FakeEngine Engine, TransfersViewModel Transfers, List<(string Title, string Body)> Notes) Make()
    {
        var engine = new FakeEngine();
        var notes = new List<(string Title, string Body)>();
        var transfers = new TransfersViewModel(engine, _ => "Laptop", (title, body) => notes.Add((title, body)));
        return (engine, transfers, notes);
    }

    /// <summary>Item <paramref name="index"/> of drag <paramref name="dragId"/>, as SEND_PROGRESS names it.</summary>
    private static ulong Item(uint dragId, uint index) => ((ulong)dragId << 32) | index;

    [Fact]
    public void AReceivedFileShowsItsProgressThenEndsDoneAndNotifies()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 7, DeviceId: _laptop, Name: "photo.jpg", BytesTotal: 400));
        TransferRow row = Assert.Single(transfers.Rows);
        Assert.True(transfers.HasRows);
        Assert.False(row.IsSending);
        Assert.Equal("photo.jpg", row.Name);
        Assert.Equal("Laptop", row.Peer);

        engine.Raise(new EngineEvent(EngineEventKind.TransferProgress, Connection: 7, BytesDone: 100, BytesTotal: 400));
        Assert.Equal(25, row.Percent);
        engine.Raise(new EngineEvent(EngineEventKind.TransferProgress, Connection: 99, BytesDone: 5, BytesTotal: 9));
        Assert.Single(transfers.Rows); // a clipboard fetch: not listed

        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 7, Reason: CoreResult.Ok));
        Assert.Equal(TransferState.Done, row.State);
        Assert.Equal(100, row.Percent);
        Assert.False(row.CancelCommand.CanExecute(null));
        (string title, _) = Assert.Single(notes);
        Assert.Equal(Localization.Strings.TransfersNotifyReceived, title);
    }

    [Fact]
    public void LinksAFolderLeftOutAreNamedOnItsRowAndInItsNotification()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 8, DeviceId: _laptop, Name: "Photos", BytesTotal: 10));
        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 8, Reason: CoreResult.Ok, Code: 2));
        TransferRow row = Assert.Single(transfers.Rows);
        Assert.True(row.HasLinksNote);
        Assert.Equal(string.Format(System.Globalization.CultureInfo.CurrentCulture, Localization.Strings.TransfersLinksLeftOut, "2"), row.LinksNote);
        Assert.EndsWith(row.LinksNote, Assert.Single(notes).Body, StringComparison.Ordinal);

        // Reported before the transfer was listed, and a single one.
        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 9, Reason: CoreResult.Ok, Code: 1));
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 9, DeviceId: _laptop, Name: "Notes", BytesTotal: 10));
        Assert.Equal(Localization.Strings.TransfersLinkLeftOut, transfers.Rows[0].LinksNote);
    }

    [Fact]
    public void AnEndThatCameBeforeTheTransferWasListedIsApplied()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 3, Reason: CoreResult.ConnectionClosed));
        Assert.Empty(transfers.Rows);
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 3, DeviceId: _laptop, Name: "a.zip", BytesTotal: 10));
        Assert.Equal(TransferState.Failed, Assert.Single(transfers.Rows).State);
        Assert.Equal(Localization.Strings.TransfersNotifyNotReceived, Assert.Single(notes).Title);
    }

    [Fact]
    public async Task CancellingAReceivedFileCancelsItsTransferWithoutANotification()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 4, DeviceId: _laptop, Name: "big.iso", BytesTotal: 10));
        TransferRow row = Assert.Single(transfers.Rows);
        await row.CancelCommand.ExecuteAsync(null);
        Assert.Equal([4UL], engine.CancelledTransfers);
        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 4, Reason: CoreResult.Cancelled));
        Assert.Equal(TransferState.Cancelled, row.State);
        Assert.Empty(notes);
    }

    [Fact]
    public async Task ASentItemIsListedPerComputerAndCancellingWithdrawsTheDrag()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 1, Connection: Item(12, 1), DeviceId: _laptop, Name: "report.pdf",
            BytesDone: 0, BytesTotal: 50));
        TransferRow row = Assert.Single(transfers.Rows);
        Assert.True(row.IsSending);
        Assert.Equal("report.pdf", row.Name);

        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 1, Connection: Item(12, 1), DeviceId: _laptop, BytesDone: 25, BytesTotal: 50));
        Assert.Equal(50, row.Percent);

        await row.CancelCommand.ExecuteAsync(null);
        Assert.Equal([12U], engine.WithdrawnDrags);
        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 3, Connection: Item(12, 1), DeviceId: _laptop, BytesDone: 25, BytesTotal: 50));
        Assert.Equal(TransferState.Cancelled, row.State);
        Assert.Empty(notes);
    }

    [Fact]
    public void ASendThatStopsByItselfFailsAndOneThatEndsUnlistedIsNotShown()
    {
        (FakeEngine engine, TransfersViewModel transfers, var notes) = Make();
        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 2, Connection: Item(1, 0), DeviceId: _laptop, BytesDone: 5, BytesTotal: 5));
        Assert.Empty(transfers.Rows);

        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 1, Connection: Item(2, 0), DeviceId: _laptop, BytesTotal: 5));
        engine.Raise(new EngineEvent(
            EngineEventKind.SendProgress, Code: 3, Connection: Item(2, 0), DeviceId: _laptop, BytesDone: 1, BytesTotal: 5));
        Assert.Equal(TransferState.Failed, Assert.Single(transfers.Rows).State);
        Assert.Equal(Localization.Strings.TransfersNotifyNotSent, Assert.Single(notes).Title);
        Assert.Equal(Localization.Strings.TransfersCopiedFiles, transfers.Rows[0].Name);
    }

    [Fact]
    public void RunningTransfersComeFirstAndOnlyTheNewestEndedOnesStay()
    {
        (FakeEngine engine, TransfersViewModel transfers, _) = Make();
        for (ulong id = 1; id <= AppTunables.TransfersEndedRows + 2; id++)
        {
            engine.Raise(new EngineEvent(
                EngineEventKind.TransferReceiving, Connection: id, DeviceId: _laptop, Name: $"{id}", BytesTotal: 1));
            engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: id, Reason: CoreResult.Ok));
        }
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: 100, DeviceId: _laptop, Name: "running", BytesTotal: 1));
        Assert.Equal(AppTunables.TransfersEndedRows + 1, transfers.Rows.Count);
        Assert.Equal("running", transfers.Rows[0].Name);
        Assert.Equal($"{AppTunables.TransfersEndedRows + 2}", transfers.Rows[1].Name); // the newest ended
    }
}
