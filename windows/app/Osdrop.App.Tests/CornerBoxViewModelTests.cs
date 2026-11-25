using Osdrop.App.Corners;
using Osdrop.App.Engine;
using Osdrop.App.Interop;
using Osdrop.App.Transfers;
using Xunit;

namespace Osdrop.App.Tests;

/// <summary>A phone's box (DEVPLAN Phase 21), with a fake engine: when it shows, what it lists, when it closes.</summary>
public sealed class CornerBoxViewModelTests
{
    private const string _phone = "50505050505050505050505050505050";
    private const string _laptop = "31313131313131313131313131313131";

    private static (FakeEngine Engine, CornerBoxViewModel Box, List<TaskCompletionSource> Delays) Make()
    {
        var engine = new FakeEngine();
        var transfers = new TransfersViewModel(engine, _ => "Phone", (_, _) => { });
        var delays = new List<TaskCompletionSource>();
        var box = new CornerBoxViewModel(_phone, () => "Phone", transfers, (length, cancellation) =>
        {
            Assert.Equal(AppTunables.CornerSentLinger, length);
            var delay = new TaskCompletionSource();
            _ = cancellation.Register(() => delay.TrySetCanceled(cancellation));
            delays.Add(delay);
            return delay.Task;
        });
        return (engine, box, delays);
    }

    private static void Receive(FakeEngine engine, ulong transfer, string from) =>
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferReceiving, Connection: transfer, DeviceId: from, Name: "photo.jpg", BytesTotal: 10));

    [Fact]
    public void ADragOverTheCornerShowsTheBoxUntilItLeaves()
    {
        (_, CornerBoxViewModel box, _) = Make();
        Assert.Equal(CornerBoxMode.Hidden, box.Mode);
        box.Entered();
        Assert.Equal(CornerBoxMode.Hovering, box.Mode);
        Assert.True(box.ShowsHint);
        box.Left();
        Assert.Equal(CornerBoxMode.Hidden, box.Mode);
    }

    [Fact]
    public void TheBoxListsOnlyThePhonesTransfersSinceItOpened()
    {
        (FakeEngine engine, CornerBoxViewModel box, _) = Make();
        Receive(engine, 1, _phone); // before the box opened
        Receive(engine, 2, _phone);
        box.Open(TransferKey.Receive(2)); // the transfer that opened it is its own
        Receive(engine, 3, _laptop);
        Receive(engine, 4, _phone);
        Assert.Equal([4UL, 2UL], box.Rows.Select(row => row.Key.Id).Order().Reverse());
        Assert.True(box.ShowsRows);
    }

    [Fact]
    public void AReceivedFileCanBeDraggedOutOnceItArrived()
    {
        (FakeEngine engine, CornerBoxViewModel box, _) = Make();
        box.Open();
        Receive(engine, 5, _phone);
        Assert.False(box.HasFiles);
        engine.Raise(new EngineEvent(
            EngineEventKind.TransferFinished, Connection: 5, Reason: CoreResult.Ok, Message: @"C:\Users\a\Downloads\photo.jpg"));
        Assert.True(box.HasFiles);
        Assert.Equal(@"C:\Users\a\Downloads\photo.jpg", Assert.Single(box.Rows).File);
    }

    [Fact]
    public void ABoxThatOnlySentFilesClosesAMomentAfterTheLastEnded()
    {
        (FakeEngine engine, CornerBoxViewModel box, var delays) = Make();
        box.Open();
        engine.Raise(new EngineEvent(
            EngineEventKind.SentProgress, Code: 1, Connection: 1UL << 32, DeviceId: _phone, Name: "a.pdf", BytesTotal: 10));
        Assert.Empty(delays); // still sending
        engine.Raise(new EngineEvent(
            EngineEventKind.SentProgress, Code: 2, Connection: 1UL << 32, DeviceId: _phone, BytesDone: 10, BytesTotal: 10));
        delays[^1].SetResult();
        Assert.Equal(CornerBoxMode.Hidden, box.Mode);
    }

    [Fact]
    public void ABoxWithAReceivedFileStaysOpen()
    {
        (FakeEngine engine, CornerBoxViewModel box, var delays) = Make();
        box.Open();
        Receive(engine, 6, _phone);
        engine.Raise(new EngineEvent(EngineEventKind.TransferFinished, Connection: 6, Reason: CoreResult.Ok, Message: "x"));
        Assert.Empty(delays);
        box.CloseCommand.Execute(null);
        Assert.Equal(CornerBoxMode.Hidden, box.Mode);
    }
}
