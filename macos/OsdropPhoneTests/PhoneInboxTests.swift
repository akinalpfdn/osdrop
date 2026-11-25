import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// Files a computer sends the iPhone: every item is fetched, and the answer goes once all ended.
@MainActor
@Suite("Phone inbox")
struct PhoneInboxTests {
    private let mac = phoneTestDeviceID(2)

    private func items(_ count: Int) -> [ArrivingItem] {
        (0..<count).map { ArrivingItem(name: "f\($0)", isFolder: false, byteCount: 1, contentType: "public.data") }
    }

    private final class Recorder {
        var fetched: [TransferItem] = []
        var answers: [(UInt64, SendOutcome)] = []
    }

    private func inbox(_ recorder: Recorder, failing: Set<UInt32> = []) -> PhoneInbox {
        var next: UInt64 = 100
        return PhoneInbox(
            receive: { item, _, _ throws(CoreError) in
                if failing.contains(item.index) { throw .ioFailure }
                recorder.fetched.append(item)
                next += 1
                return next
            },
            answer: { sendID, _, outcome in recorder.answers.append((sendID, outcome)) },
            transfers: PhoneTransfers(), folder: URL.temporaryDirectory.appending(path: "inbox-tests"))
    }

    @Test("Every item is fetched and the send is answered received once they arrived")
    func received() {
        let recorder = Recorder()
        let inbox = inbox(recorder)
        inbox.arrived(from: mac, sendID: 4, items: items(2))
        #expect(recorder.fetched.map(\.index) == [0, 1])
        #expect(recorder.answers.isEmpty)
        inbox.finished(101, succeeded: true)
        inbox.finished(102, succeeded: true)
        #expect(recorder.answers.count == 1)
        #expect(recorder.answers.first?.0 == 4)
        #expect(recorder.answers.first?.1 == .received)
    }

    @Test("A send with an item that failed is answered failed")
    func failed() {
        let recorder = Recorder()
        let inbox = inbox(recorder, failing: [1])
        inbox.arrived(from: mac, sendID: 6, items: items(2))
        inbox.finished(101, succeeded: true)
        #expect(recorder.answers.first?.1 == .failed)
    }

    @Test("The same send arriving twice is fetched once")
    func duplicate() {
        let recorder = Recorder()
        let inbox = inbox(recorder)
        inbox.arrived(from: mac, sendID: 8, items: items(1))
        inbox.arrived(from: mac, sendID: 8, items: items(1))
        #expect(recorder.fetched.count == 1)
    }
}
