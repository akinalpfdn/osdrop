import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The transfers list: receiving and sending rows with their progress, cancelling, the end of
/// each with a notification, and how many ended rows stay.
@MainActor
@Suite("Transfers in the menu")
struct TransfersControllerTests {
    private let notifier = FakeNotifier()
    private let peer = transferDeviceID(0x42)
    private let model: TransfersController
    private let cancelledReceives = Box<[UInt64]>([])
    private let withdrawnDrags = Box<[UInt32]>([])

    init() {
        let cancelled = cancelledReceives
        let withdrawn = withdrawnDrags
        model = TransfersController(
            nameOf: { _ in "PC" }, notifier: notifier, cancelReceiving: { cancelled.value.append($0) },
            withdrawDrag: { withdrawn.value.append($0) })
    }

    @Test("A drag that arrives is shown with its progress until it is received, with a notification")
    func receives() {
        model.receiving(7, name: "Report.pdf", from: peer, bytes: 100)
        model.handle(.transferProgress(progress(id: 7, done: 40, total: 100)))
        #expect(model.rows.first?.fraction == 0.4)
        model.handle(.transferFinished(id: 7, .success(ReceivedItem(items: [], skippedLinks: 0))))
        #expect(model.rows.first?.state == .done)
        #expect(model.rows.first?.fraction == 1)
        #expect(notifier.notifications.count == 1)
    }

    @Test("Links a received folder left out are shown on its row and in its notification")
    func namesLinksLeftOut() throws {
        model.receiving(8, name: "Photos", from: peer, bytes: 10)
        model.handle(.transferFinished(id: 8, .success(ReceivedItem(items: [], skippedLinks: 2))))
        let row = try #require(model.rows.first)
        #expect(row.linksLeftOut == 2)
        let note = TransfersController.linksNote(2)
        #expect(note.contains("2"))
        #expect(notifier.notifications.last?.hasSuffix("\n" + note) == true)
    }

    @Test("An end reported before the transfer was registered still ends it")
    func earlyEnd() {
        model.handle(.transferFinished(id: 7, .failure(.connectionClosed)))
        model.receiving(7, name: "Report.pdf", from: peer, bytes: 100)
        #expect(model.rows.first?.state == .failed)
    }

    @Test("Cancelling a received transfer cancels it, and no notification follows")
    func cancelsReceiving() throws {
        model.receiving(7, name: "Report.pdf", from: peer, bytes: 100)
        let row = try #require(model.rows.first)
        model.cancel(row)
        #expect(cancelledReceives.value == [7])
        model.handle(.transferFinished(id: 7, .failure(.cancelled)))
        #expect(model.rows.first?.state == .cancelled)
        #expect(notifier.notifications.isEmpty)
    }

    @Test("Sending this Mac's drag shows the item's name; cancelling withdraws the drag")
    func sends() throws {
        model.offered(dragID: 3, names: ["Report.pdf", "Photos"])
        let item = TransferItem(kind: .drag, objectID: 3, index: 1)
        model.handle(
            .sendProgress(SendProgress(device: peer, item: item, bytesSent: 10, bytesTotal: 40, state: .sending)))
        let row = try #require(model.rows.first)
        #expect(row.name == "Photos")
        #expect(row.isSending)
        model.cancel(row)
        #expect(withdrawnDrags.value == [3])
        model.handle(
            .sendProgress(SendProgress(device: peer, item: item, bytesSent: 10, bytesTotal: 40, state: .stopped)))
        #expect(model.rows.first?.state == .cancelled)
        model.handle(
            .sendProgress(SendProgress(device: peer, item: item, bytesSent: 40, bytesTotal: 40, state: .sending)))
        #expect(model.rows.count == 1, "an ended row is not revived")
    }

    @Test("A send that stops by itself fails with a notification; one done notifies too")
    func sendEnds() {
        let first = TransferItem(kind: .drag, objectID: 3, index: 0)
        let second = TransferItem(kind: .clipboard, objectID: 9, index: 5)
        model.handle(
            .sendProgress(SendProgress(device: peer, item: first, bytesSent: 1, bytesTotal: 4, state: .sending)))
        model.handle(
            .sendProgress(SendProgress(device: peer, item: first, bytesSent: 1, bytesTotal: 4, state: .stopped)))
        model.handle(
            .sendProgress(SendProgress(device: peer, item: second, bytesSent: 1, bytesTotal: 4, state: .sending)))
        model.handle(.sendProgress(SendProgress(device: peer, item: second, bytesSent: 4, bytesTotal: 4, state: .done)))
        #expect(model.rows.map(\.state) == [.done, .failed])
        #expect(model.rows.first?.name == String(localized: "transfers.copiedFiles"))
        #expect(notifier.notifications.count == 2)
    }

    @Test("Only the latest ended transfers stay, after the running ones")
    func keepsTheLatestEnded() {
        for id in UInt64(1)...UInt64(Tunables.Transfers.endedRows + 2) {
            model.receiving(id, name: "\(id)", from: peer, bytes: 1)
            model.handle(.transferFinished(id: id, .success(ReceivedItem(items: [], skippedLinks: 0))))
        }
        model.receiving(99, name: "running", from: peer, bytes: 1)
        #expect(model.rows.count == Tunables.Transfers.endedRows + 1)
        #expect(model.rows.first?.name == "running")
        #expect(model.rows[1].name == "\(Tunables.Transfers.endedRows + 2)", "the newest ended first")
    }

    private func progress(id: UInt64, done: UInt64, total: UInt64) -> TransferProgress {
        TransferProgress(id: id, bytesDone: done, bytesTotal: total, filesDone: 0, filesTotal: 1)
    }
}

/// A value a closure can change, for the tests.
@MainActor
private final class Box<Value> {
    var value: Value

    init(_ value: Value) {
        self.value = value
    }
}

private func transferDeviceID(_ byte: UInt8) -> DeviceID {
    guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
        preconditionFailure("16 bytes make a device ID")
    }
    return id
}
