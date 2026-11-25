import CoreBridge
import Foundation

/// Files a computer sends this phone (DEVPLAN Phase 22): each item is fetched into the app's
/// folder in the Files app and listed, and the computer gets the answer once every item ended.
/// The phone takes every send, as the Android app does.
@MainActor
final class PhoneInbox {
    private struct Arrival {
        let device: DeviceID
        var pending: Set<UInt64> = []
        var failed = false
        /// Every item was asked for; the answer waits for this.
        var started = false
    }

    typealias Receive = @MainActor (TransferItem, DeviceID, URL) throws(CoreError) -> UInt64
    typealias Answer = @MainActor (UInt64, DeviceID, SendOutcome) -> Void

    private let receive: Receive
    private let answer: Answer
    private let transfers: PhoneTransfers
    private let folder: URL
    private var arrivals: [UInt64: Arrival] = [:]  // by send ID
    private var sendOf: [UInt64: UInt64] = [:]  // transfer → send
    /// Transfers that ended before they were recorded: whether they succeeded.
    private var early: [UInt64: Bool] = [:]

    init(receive: @escaping Receive, answer: @escaping Answer, transfers: PhoneTransfers, folder: URL) {
        self.receive = receive
        self.answer = answer
        self.transfers = transfers
        self.folder = folder
    }

    func arrived(from device: DeviceID, sendID: UInt64, items: [ArrivingItem]) {
        guard arrivals[sendID] == nil else { return }
        var arrival = Arrival(device: device)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for (index, item) in items.enumerated() {
            let sent = TransferItem(kind: .sent, objectID: sendID, index: UInt32(index))
            do throws(CoreError) {
                let id = try receive(sent, device, folder)
                transfers.receiving(id, name: item.name, from: device, bytes: item.byteCount)
                if let succeeded = early.removeValue(forKey: id) {
                    arrival.failed = arrival.failed || !succeeded
                } else {
                    arrival.pending.insert(id)
                    sendOf[id] = sendID
                }
            } catch {
                Log.files.error("Fetching a sent item failed: \(String(describing: error), privacy: .public)")
                arrival.failed = true
            }
        }
        arrival.started = true
        arrivals[sendID] = arrival
        settle(sendID)
    }

    /// A transfer ended; one of a send's items, or not.
    func finished(_ id: UInt64, succeeded: Bool) {
        guard let sendID = sendOf.removeValue(forKey: id) else {
            early[id] = succeeded
            if early.count > Tunables.Transfers.earlyEnds, let oldest = early.keys.min() {
                early[oldest] = nil
            }
            return
        }
        arrivals[sendID]?.pending.remove(id)
        if !succeeded {
            arrivals[sendID]?.failed = true
        }
        settle(sendID)
    }

    /// The mesh stopped: sends still arriving end with it.
    func reset() {
        arrivals = [:]
        sendOf = [:]
        early = [:]
    }

    private func settle(_ sendID: UInt64) {
        guard let arrival = arrivals[sendID], arrival.started, arrival.pending.isEmpty else { return }
        arrivals[sendID] = nil
        answer(sendID, arrival.device, arrival.failed ? .failed : .received)
    }
}
