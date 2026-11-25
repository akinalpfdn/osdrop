import CoreBridge
import Foundation
import Observation

/// Transfers with the computers, both ways, newest first, as the home screen lists them.
@MainActor
@Observable
final class PhoneTransfers {
    struct Row: Identifiable, Equatable {
        enum Key: Hashable {
            /// A transfer this phone receives.
            case incoming(UInt64)
            /// Item `index` of a send of this phone.
            case outgoing(sendID: UInt64, index: UInt32)
        }

        enum State: Equatable {
            case running, done, failed, cancelled
        }

        let id: Key
        let name: String
        let peer: DeviceID
        var state: State = .running
        var bytesDone: UInt64 = 0
        var bytesTotal: UInt64
        /// A received item as it was created, once it arrived.
        var file: URL?

        var isSending: Bool {
            if case .outgoing = id { return true }
            return false
        }

        var fraction: Double {
            bytesTotal == 0 ? (state == .done ? 1 : 0) : min(Double(bytesDone) / Double(bytesTotal), 1)
        }
    }

    private(set) var rows: [Row] = []
    /// Item names of this phone's sends, until their rows are listed.
    @ObservationIgnored private var sendNames: [UInt64: (peer: DeviceID, names: [String])] = [:]
    /// Ends of incoming transfers reported before they were listed.
    @ObservationIgnored private var early: [UInt64: Result<ReceivedItem, CoreError>] = [:]
    /// Rows the user cancelled: they end as cancelled, not failed.
    @ObservationIgnored private var cancelled: Set<Row.Key> = []

    // MARK: - Incoming

    func receiving(_ id: UInt64, name: String, from peer: DeviceID, bytes: UInt64) {
        insert(Row(id: .incoming(id), name: name, peer: peer, bytesTotal: bytes))
        if let ended = early.removeValue(forKey: id) {
            finished(id, ended)
        }
    }

    func progress(_ progress: TransferProgress) {
        update(.incoming(progress.id)) {
            $0.bytesDone = progress.bytesDone
            $0.bytesTotal = progress.bytesTotal
        }
    }

    func finished(_ id: UInt64, _ result: Result<ReceivedItem, CoreError>) {
        let key = Row.Key.incoming(id)
        guard rows.contains(where: { $0.id == key }) else {
            early[id] = result
            if early.count > Tunables.Transfers.earlyEnds, let oldest = early.keys.min() {
                early[oldest] = nil
            }
            return
        }
        switch result {
        case .success(let item):
            end(key, .done) { $0.file = item.items.first }
        case .failure(let error):
            end(key, error == .cancelled && cancelled.contains(key) ? .cancelled : .failed)
        }
    }

    // MARK: - Outgoing

    /// A send of this phone began; its rows show as its items are fetched.
    func offered(_ sendID: UInt64, to peer: DeviceID, names: [String]) {
        sendNames[sendID] = (peer, names)
    }

    func sent(_ progress: SendProgress) {
        guard progress.item.kind == .sent else { return }
        let key = Row.Key.outgoing(sendID: progress.item.objectID, index: progress.item.index)
        if !rows.contains(where: { $0.id == key }) {
            guard progress.state == .sending else { return }  // ended before it was listed
            let names = sendNames[progress.item.objectID]?.names ?? []
            let index = Int(progress.item.index)
            let name = names.indices.contains(index) ? names[index] : ""
            insert(Row(id: key, name: name, peer: progress.device, bytesTotal: progress.bytesTotal))
        }
        update(key) {
            $0.bytesDone = progress.bytesSent
            $0.bytesTotal = progress.bytesTotal
        }
        switch progress.state {
        case .sending: break
        case .done: end(key, .done)
        case .stopped: end(key, cancelled.contains(key) ? .cancelled : .failed)
        }
    }

    /// A send ended: when the computer did not take it, its rows still running fail.
    func sendFinished(_ sendID: UInt64, _ outcome: SendOutcome) {
        sendNames[sendID] = nil
        guard outcome != .received else { return }
        for row in rows where row.state == .running {
            if case .outgoing(let id, _) = row.id, id == sendID {
                end(row.id, cancelled.contains(row.id) ? .cancelled : .failed)
            }
        }
    }

    // MARK: - Cancelling

    /// The user cancelled the row: it ends as cancelled once the mesh confirms.
    func markCancelled(_ key: Row.Key) {
        cancelled.insert(key)
    }

    // MARK: - Rows

    /// Above the rows that ended.
    private func insert(_ row: Row) {
        let index = rows.firstIndex { $0.state != .running } ?? rows.endIndex
        rows.insert(row, at: index)
    }

    private func update(_ key: Row.Key, _ change: (inout Row) -> Void) {
        guard let index = rows.firstIndex(where: { $0.id == key }), rows[index].state == .running else { return }
        change(&rows[index])
    }

    private func end(_ key: Row.Key, _ state: Row.State, _ change: (inout Row) -> Void = { _ in }) {
        guard let index = rows.firstIndex(where: { $0.id == key }), rows[index].state == .running else { return }
        var row = rows.remove(at: index)
        row.state = state
        if state == .done {
            row.bytesDone = row.bytesTotal
        }
        change(&row)
        cancelled.remove(key)
        insert(row)
        let ended = rows.indices.filter { rows[$0].state != .running }
        if ended.count > Tunables.Transfers.endedRows, let last = ended.last {
            rows.remove(at: last)
        }
    }
}
