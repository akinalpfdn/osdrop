import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// Files a phone sends are fetched item by item, and the phone hears back once every item ended.
@MainActor
@Suite("Send inbox")
struct SendInboxTests {
    private let phone: DeviceID
    private let log = Log()

    init() throws {
        phone = try #require(DeviceID(bytes: [UInt8](repeating: 7, count: 16)))
    }

    @Test("Every item is fetched, and the answer waits for the last one")
    func answersOnceEveryItemArrived() async throws {
        let inbox = makeInbox()
        inbox.arrived(from: phone, sendID: 9, items: [item("a.jpg"), item("b.apk")])
        try await until { log.fetched.count == 2 }
        #expect(log.fetched.map(\.index) == [0, 1])
        #expect(log.fetched.allSatisfy { $0.kind == .sent && $0.objectID == 9 })

        inbox.finished(100, succeeded: true)
        try await Task.sleep(for: .milliseconds(50))
        #expect(log.answers.isEmpty, "one item is still on its way")
        inbox.finished(101, succeeded: true)
        try await until { !log.answers.isEmpty }
        #expect(log.answers == [Answered(sendID: 9, outcome: .received)])
    }

    @Test("An item that fails, even one that ended before it was recorded, fails the send")
    func failedItemFailsTheSend() async throws {
        let inbox = makeInbox()
        inbox.finished(100, succeeded: false)  // ends before receive() returned
        inbox.arrived(from: phone, sendID: 4, items: [item("a.jpg"), item("b.jpg")])
        try await until { log.fetched.count == 2 }
        inbox.finished(101, succeeded: true)
        try await until { !log.answers.isEmpty }
        #expect(log.answers == [Answered(sendID: 4, outcome: .failed)])
    }

    private func makeInbox() -> SendInbox {
        let log = log
        return SendInbox(
            receive: { item, _, _ throws(CoreError) in
                log.fetched.append(item)
                return 100 + UInt64(item.index)
            },
            answer: { sendID, _, outcome in log.answers.append(Answered(sendID: sendID, outcome: outcome)) },
            transfers: { nil }, folder: { URL(filePath: "/tmp") })
    }

    private func item(_ name: String) -> ArrivingItem {
        ArrivingItem(name: name, isFolder: false, byteCount: 1, contentType: "public.data")
    }

    private func until(_ condition: () -> Bool) async throws {
        for _ in 0..<100 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

private struct Answered: Equatable {
    let sendID: UInt64
    let outcome: SendOutcome
}

@MainActor
private final class Log {
    var fetched: [TransferItem] = []
    var answers: [Answered] = []
}
