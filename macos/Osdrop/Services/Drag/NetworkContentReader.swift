import CoreBridge
import Foundation

/// Fetches one item of a drag that came from another computer.
protocol DragItemFetching: Sendable {
    /// Receives item `index` into `folder`, which is empty, and returns the files and folders it
    /// created there. Waits until the transfer ends; cancelling `progress` cancels it. `started`
    /// gets the transfer's ID once it runs.
    func fetch(
        _ index: UInt32, into folder: URL, progress: Progress, started: @escaping @Sendable (UInt64) -> Void
    ) throws -> [URL]
}

/// Content for the file promises of a drag that came from another computer: each item is fetched
/// through the transfer engine when the drop target asks for it (DEVPLAN Phase 14).
struct NetworkContentReader: PromisedContentReading {
    let fetcher: any DragItemFetching
    /// The item's place in the drag, by the ID of its `DragItem`.
    let indices: [UUID: UInt32]
    /// A transfer for the item started.
    var started: @Sendable (DragItem, UInt64) -> Void = { _, _ in }

    func write(_ item: DragItem, to destination: URL, progress: Progress) throws {
        guard let index = indices[item.id] else {
            throw CocoaError(.fileNoSuchFile)
        }
        // Received beside the destination, on its volume, and moved there as a whole.
        let folder = destination.deletingLastPathComponent()
            .appendingPathComponent(".osdrop-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: false)
        defer { try? FileManager.default.removeItem(at: folder) }  // empty after a move or a failed transfer
        let received = try fetcher.fetch(index, into: folder, progress: progress) { started(item, $0) }
        guard received.count == 1, let first = received.first else {
            throw CocoaError(.fileReadCorruptFile)
        }
        try FileManager.default.moveItem(at: first, to: destination)
    }
}

/// Fetches drag items through the mesh. Called by AppKit on a promise queue, sometimes on the main
/// thread (a staged copy); it waits on that thread, and the transfer's end reaches it without the
/// main thread.
struct MeshDragFetcher: DragItemFetching {
    let mesh: any TransferDriving
    let source: DeviceID
    let dragID: UInt32

    func fetch(
        _ index: UInt32, into folder: URL, progress: Progress, started: @escaping @Sendable (UInt64) -> Void
    ) throws -> [URL] {
        let outcome = Locked<Result<[URL], CoreError>?>(nil)
        let done = DispatchSemaphore(value: 0)
        let finish: @Sendable (Result<[URL], CoreError>) -> Void = { result in
            outcome.withLock { $0 = result }
            done.signal()
        }
        let item = TransferItem(kind: .drag, objectID: UInt64(dragID), index: index)
        let tracked = TrackedProgress(progress)
        Task { [mesh, source] in
            let id: UInt64
            do throws(CoreError) {
                id = try await mesh.receive(item, from: source, into: folder, maxBytes: .max)
            } catch {
                finish(.failure(error))
                return
            }
            started(id)
            tracked.onCancel { Task { await mesh.cancelTransfer(id) } }
            mesh.transferWatch.watch(
                id, progress: { tracked.set(done: $0.bytesDone) },
                finished: { finish($0.map(\.items)) })
        }
        done.wait()
        guard let result = outcome.withLock({ $0 }) else {
            throw CocoaError(.fileReadUnknown)
        }
        return try result.get()
    }
}

/// A `Progress` updated from core's thread. `Progress` is thread-safe, but not marked `Sendable`.
private final class TrackedProgress: @unchecked Sendable {
    private let progress: Progress

    init(_ progress: Progress) {
        self.progress = progress
    }

    func set(done bytes: UInt64) {
        progress.completedUnitCount = Int64(clamping: bytes)
    }

    /// Runs `cancel` when the progress is cancelled, or at once when it already was.
    func onCancel(_ cancel: @escaping @Sendable () -> Void) {
        progress.cancellationHandler = cancel
        if progress.isCancelled {
            cancel()
        }
    }
}
