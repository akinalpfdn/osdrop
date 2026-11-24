import CoreBridge
import Foundation

/// Files a member sent this Mac without a drag (a phone; DEVPLAN Phase 20): each item is fetched
/// into Downloads and shown in the transfers list, and the sender gets the answer once every item
/// ended.
@MainActor
final class SendInbox {
    private struct Arrival {
        let device: DeviceID
        var pending: Set<UInt64> = []
        var failed = false
        /// Every item was asked for; the answer waits for this.
        var started = false
    }

    typealias Receive = @MainActor (TransferItem, DeviceID, URL) async throws(CoreError) -> UInt64
    typealias Answer = @MainActor (UInt64, DeviceID, SendOutcome) async -> Void

    private let receive: Receive
    private let answer: Answer
    private let transfers: () -> TransfersController?
    private let folder: () -> URL
    private var arrivals: [UInt64: Arrival] = [:]  // by send ID
    private var sendOf: [UInt64: UInt64] = [:]  // transfer → send
    /// Transfers that ended before they were recorded: whether they succeeded.
    private var early: [UInt64: Bool] = [:]

    init(
        receive: @escaping Receive, answer: @escaping Answer, transfers: @escaping () -> TransfersController?,
        folder: @escaping () -> URL
    ) {
        self.receive = receive
        self.answer = answer
        self.transfers = transfers
        self.folder = folder
    }

    /// Fetches through `mesh` and answers through it.
    convenience init(
        mesh: any MeshDriving, transfers: @escaping () -> TransfersController?, folder: @escaping () -> URL
    ) {
        self.init(
            receive: { item, device, folder throws(CoreError) in
                try await mesh.receive(item, from: device, into: folder, maxBytes: .max)
            },
            answer: { sendID, device, outcome in
                do throws(CoreError) {
                    try await mesh.answerSend(sendID, from: device, outcome)
                } catch {
                    Log.drag.error("Answering a send failed: \(String(describing: error), privacy: .public)")
                }
            }, transfers: transfers, folder: folder)
    }

    func arrived(from device: DeviceID, sendID: UInt64, items: [ArrivingItem]) {
        guard arrivals[sendID] == nil else { return }
        arrivals[sendID] = Arrival(device: device)
        Task {
            for (index, item) in items.enumerated() {
                let sent = TransferItem(kind: .sent, objectID: sendID, index: UInt32(index))
                do throws(CoreError) {
                    let id = try await receive(sent, device, folder())
                    transfers()?.receiving(id, name: item.name, from: device, bytes: item.byteCount)
                    if let succeeded = early.removeValue(forKey: id) {
                        if !succeeded {
                            arrivals[sendID]?.failed = true
                        }
                    } else {
                        arrivals[sendID]?.pending.insert(id)
                        sendOf[id] = sendID
                    }
                } catch {
                    Log.drag.error("Fetching a sent item failed: \(String(describing: error), privacy: .public)")
                    arrivals[sendID]?.failed = true
                }
            }
            arrivals[sendID]?.started = true
            settle(sendID)
        }
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

    private func settle(_ sendID: UInt64) {
        guard let arrival = arrivals[sendID], arrival.started, arrival.pending.isEmpty else { return }
        arrivals[sendID] = nil
        Task { await answer(sendID, arrival.device, arrival.failed ? .failed : .received) }
    }
}
