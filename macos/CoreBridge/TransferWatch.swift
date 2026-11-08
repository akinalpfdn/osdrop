import Foundation

/// Follows transfers by their ID on core's own thread, for code that waits for one away from the
/// main thread: a drop target asking for a file that is still on another computer. Every event
/// also reaches `Mesh.events`.
public final class TransferWatch: @unchecked Sendable {  // its state is guarded by `lock`
    private struct Watcher {
        let progress: @Sendable (TransferProgress) -> Void
        let finished: @Sendable (Result<ReceivedItem, CoreError>) -> Void
    }

    /// Transfers that may end before anyone watches them: a receive returns their ID only after
    /// it started.
    static let earlyLimit = 64

    private let lock = NSLock()
    private var watchers: [UInt64: Watcher] = [:]
    private var early: [(id: UInt64, result: Result<ReceivedItem, CoreError>)] = []

    init() {}

    /// Calls `progress` with each report of transfer `id`, and `finished` once at its end; at once
    /// when it has already ended. Both run on core's thread and must return quickly.
    public func watch(
        _ id: UInt64, progress: @escaping @Sendable (TransferProgress) -> Void,
        finished: @escaping @Sendable (Result<ReceivedItem, CoreError>) -> Void
    ) {
        lock.lock()
        if let index = early.firstIndex(where: { $0.id == id }) {
            let result = early.remove(at: index).result
            lock.unlock()
            finished(result)
            return
        }
        watchers[id] = Watcher(progress: progress, finished: finished)
        lock.unlock()
    }

    func progressed(_ update: TransferProgress) {
        lock.lock()
        let watcher = watchers[update.id]
        lock.unlock()
        watcher?.progress(update)
    }

    func finished(_ id: UInt64, _ result: Result<ReceivedItem, CoreError>) {
        lock.lock()
        let watcher = watchers.removeValue(forKey: id)
        if watcher == nil {
            early.append((id, result))
            if early.count > Self.earlyLimit {
                early.removeFirst()
            }
        }
        lock.unlock()
        watcher?.finished(result)
    }
}
