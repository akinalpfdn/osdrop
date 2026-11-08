import CoreBridge
import Foundation
import Observation

/// What drag and drop tells the transfers list.
@MainActor
protocol DragTransfersTracking: AnyObject, Sendable {
    /// This Mac's drag `dragID` was offered with these items, in order.
    func offered(dragID: UInt32, names: [String])
    /// A transfer fetching an item of a drag from `source` started.
    func receiving(_ id: UInt64, name: String, from source: DeviceID, bytes: UInt64)
}

/// Shows a notification when a transfer ends.
protocol TransferNotifying: Sendable {
    func notify(title: String, body: String)
}

/// Transfers of dragged files in both directions, for the menu (DEVPLAN Phases 14 and 16): their
/// progress, a way to cancel them, and a notification when one ends.
@MainActor
@Observable
final class TransfersController: DragTransfersTracking {
    struct Row: Identifiable, Equatable {
        enum Key: Hashable {
            case receive(UInt64)
            case send(DeviceID, TransferItem)
        }

        enum State: Equatable {
            case running
            case done
            case failed
            case cancelled
        }

        let id: Key
        let name: String
        let peer: DeviceID
        var bytesDone: UInt64
        var bytesTotal: UInt64
        var state: State

        var isSending: Bool {
            if case .send = id { true } else { false }
        }

        var fraction: Double {
            guard bytesTotal > 0 else { return state == .done ? 1 : 0 }
            return min(Double(bytesDone) / Double(bytesTotal), 1)
        }
    }

    /// Running transfers, then the latest that ended.
    private(set) var rows: [Row] = []

    @ObservationIgnored private let nameOf: @MainActor (DeviceID) -> String
    @ObservationIgnored private let notifier: any TransferNotifying
    @ObservationIgnored private let cancelReceiving: @MainActor (UInt64) -> Void
    @ObservationIgnored private let withdrawDrag: @MainActor (UInt32) -> Void
    @ObservationIgnored private var dragNames: [(dragID: UInt32, names: [String])] = []
    /// Ends of transfers reported before they were registered.
    @ObservationIgnored private var early: [UInt64: Result<ReceivedItem, CoreError>] = [:]
    @ObservationIgnored private var cancelled: Set<Row.Key> = []

    init(
        nameOf: @escaping @MainActor (DeviceID) -> String, notifier: any TransferNotifying,
        cancelReceiving: @escaping @MainActor (UInt64) -> Void, withdrawDrag: @escaping @MainActor (UInt32) -> Void
    ) {
        self.nameOf = nameOf
        self.notifier = notifier
        self.cancelReceiving = cancelReceiving
        self.withdrawDrag = withdrawDrag
    }

    func name(of row: Row) -> String {
        nameOf(row.peer)
    }

    // MARK: - Drag and drop

    func offered(dragID: UInt32, names: [String]) {
        dragNames.append((dragID, names))
        if dragNames.count > Tunables.Transfers.rememberedDrags {
            dragNames.removeFirst()
        }
    }

    func receiving(_ id: UInt64, name: String, from source: DeviceID, bytes: UInt64) {
        let row = Row(id: .receive(id), name: name, peer: source, bytesDone: 0, bytesTotal: bytes, state: .running)
        rows.insert(row, at: rows.firstIndex { $0.state != .running } ?? rows.endIndex)
        if let result = early.removeValue(forKey: id) {
            finished(id, result)
        }
    }

    // MARK: - Mesh events

    func handle(_ event: MeshEvent) {
        switch event {
        case .transferProgress(let progress):
            update(.receive(progress.id)) {
                $0.bytesDone = progress.bytesDone
                $0.bytesTotal = progress.bytesTotal
            }
        case .transferFinished(let id, let result):
            finished(id, result)
        case .sendProgress(let progress):
            sent(progress)
        default:
            break
        }
    }

    func cancel(_ row: Row) {
        guard row.state == .running else { return }
        cancelled.insert(row.id)
        switch row.id {
        case .receive(let id):
            cancelReceiving(id)
        case .send(_, let item):
            withdrawDrag(UInt32(truncatingIfNeeded: item.objectID))
        }
    }

    private func finished(_ id: UInt64, _ result: Result<ReceivedItem, CoreError>) {
        guard rows.contains(where: { $0.id == .receive(id) }) else {
            early[id] = result
            if early.count > Tunables.Transfers.earlyEnds, let oldest = early.keys.min() {
                early[oldest] = nil
            }
            return
        }
        let state: Row.State =
            switch result {
            case .success: .done
            case .failure(.cancelled) where cancelled.contains(.receive(id)): .cancelled
            case .failure: .failed
            }
        end(.receive(id), as: state)
    }

    private func sent(_ progress: SendProgress) {
        let key = Row.Key.send(progress.device, progress.item)
        if !rows.contains(where: { $0.id == key }) {
            guard progress.state == .sending else { return }  // ended before it was shown
            let row = Row(
                id: key, name: name(of: progress.item), peer: progress.device, bytesDone: progress.bytesSent,
                bytesTotal: progress.bytesTotal, state: .running)
            rows.insert(row, at: rows.firstIndex { $0.state != .running } ?? rows.endIndex)
        }
        update(key) {
            $0.bytesDone = progress.bytesSent
            $0.bytesTotal = progress.bytesTotal
        }
        switch progress.state {
        case .sending: break
        case .done: end(key, as: .done)
        case .stopped: end(key, as: cancelled.contains(key) ? .cancelled : .failed)
        }
    }

    private func name(of item: TransferItem) -> String {
        guard item.kind == .drag,
            let names = dragNames.last(where: { UInt64($0.dragID) == item.objectID })?.names,
            Int(item.index) < names.count
        else {
            return String(localized: "transfers.copiedFiles")
        }
        return names[Int(item.index)]
    }

    private func update(_ key: Row.Key, _ change: (inout Row) -> Void) {
        guard let index = rows.firstIndex(where: { $0.id == key }), rows[index].state == .running else { return }
        change(&rows[index])
    }

    private func end(_ key: Row.Key, as state: Row.State) {
        guard let index = rows.firstIndex(where: { $0.id == key }), rows[index].state == .running else { return }
        var row = rows.remove(at: index)
        row.state = state
        if state == .done {
            row.bytesDone = row.bytesTotal
        }
        cancelled.remove(key)
        rows.insert(row, at: rows.firstIndex { $0.state != .running } ?? rows.endIndex)
        while rows.count(where: { $0.state != .running }) > Tunables.Transfers.endedRows,
            let oldest = rows.lastIndex(where: { $0.state != .running })
        {
            rows.remove(at: oldest)
        }
        notify(row)
    }

    private func notify(_ row: Row) {
        let peer = nameOf(row.peer)
        switch (row.isSending, row.state) {
        case (false, .done):
            notifier.notify(
                title: String(localized: "transfers.notify.received"),
                body: String(localized: "transfers.from \(row.name) \(peer)"))
        case (false, .failed):
            notifier.notify(
                title: String(localized: "transfers.notify.notReceived"),
                body: String(localized: "transfers.from \(row.name) \(peer)"))
        case (true, .done):
            notifier.notify(
                title: String(localized: "transfers.notify.sent"),
                body: String(localized: "transfers.to \(row.name) \(peer)"))
        case (true, .failed):
            notifier.notify(
                title: String(localized: "transfers.notify.notSent"),
                body: String(localized: "transfers.to \(row.name) \(peer)"))
        case (_, .running), (_, .cancelled):
            break
        }
    }
}
