import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The iPhone's transfers list (DEVPLAN Phase 22): rows both ways, how they end, and the received file.
@MainActor
@Suite("Phone transfers")
struct PhoneTransfersTests {
    private let mac = phoneTestDeviceID(1)

    @Test("A received file shows its progress, then ends done with its file")
    func receivedFile() {
        let transfers = PhoneTransfers()
        transfers.receiving(7, name: "photo.jpg", from: mac, bytes: 400)
        transfers.progress(TransferProgress(id: 7, bytesDone: 100, bytesTotal: 400, filesDone: 0, filesTotal: 1))
        #expect(transfers.rows.first?.fraction == 0.25)
        let file = URL(filePath: "/tmp/photo.jpg")
        transfers.finished(7, .success(ReceivedItem(items: [file], skippedLinks: 0)))
        #expect(transfers.rows.first?.state == .done)
        #expect(transfers.rows.first?.file == file)
    }

    @Test("An end reported before the transfer was listed is applied once it is")
    func earlyEnd() {
        let transfers = PhoneTransfers()
        transfers.finished(9, .failure(.ioFailure))
        transfers.receiving(9, name: "a.pdf", from: mac, bytes: 10)
        #expect(transfers.rows.first?.state == .failed)
    }

    @Test("A cancelled transfer ends as cancelled, not failed")
    func cancelled() {
        let transfers = PhoneTransfers()
        transfers.receiving(3, name: "video.mov", from: mac, bytes: 10)
        transfers.markCancelled(.incoming(3))
        transfers.finished(3, .failure(.cancelled))
        #expect(transfers.rows.first?.state == .cancelled)
    }

    private func offer(_ transfers: PhoneTransfers, _ sendID: UInt64) {
        transfers.offered(sendID, to: mac, files: [.init(name: "a.jpg", bytes: 8), .init(name: "b.jpg", bytes: 8)])
    }

    @Test("Sent items are listed as soon as the send begins, and fail when the computer declines")
    func sentItems() {
        let transfers = PhoneTransfers()
        offer(transfers, 5)
        #expect(transfers.rows.map(\.name).sorted() == ["a.jpg", "b.jpg"])
        #expect(transfers.rows.allSatisfy { $0.isSending && $0.state == .running })
        let item = TransferItem(kind: .sent, objectID: 5, index: 1)
        transfers.sent(SendProgress(device: mac, item: item, bytesSent: 2, bytesTotal: 8, state: .sending))
        #expect(transfers.rows.first { $0.name == "b.jpg" }?.bytesDone == 2)
        transfers.sendFinished(5, .declined)
        #expect(transfers.rows.allSatisfy { $0.state == .failed })
    }

    @Test("A small file fetched before any progress still ends done")
    func fetchedAtOnce() {
        let transfers = PhoneTransfers()
        offer(transfers, 6)
        let item = TransferItem(kind: .sent, objectID: 6, index: 0)
        transfers.sent(SendProgress(device: mac, item: item, bytesSent: 8, bytesTotal: 8, state: .done))
        #expect(transfers.rows.first { $0.name == "a.jpg" }?.state == .done)
        transfers.sendFinished(6, .received)
        #expect(transfers.rows.allSatisfy { $0.state == .done })
    }

    @Test("Running rows stay above the ones that ended")
    func runningFirst() {
        let transfers = PhoneTransfers()
        transfers.receiving(1, name: "old", from: mac, bytes: 1)
        transfers.finished(1, .success(ReceivedItem(items: [], skippedLinks: 0)))
        transfers.receiving(2, name: "new", from: mac, bytes: 1)
        #expect(transfers.rows.map(\.name) == ["new", "old"])
    }
}
