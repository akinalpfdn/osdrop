import CoreBridge
import CoreGraphics
import Foundation

@testable import Osdrop

/// Drags on this Mac as a test makes them.
@MainActor
final class FakeWatcher: DragWatching {
    var onChange: ((Bool) -> Void)?
    var files: [DraggedFile] = []
    var promises = false
    private(set) var isRunning = false

    func start() { isRunning = true }
    func stop() { isRunning = false }
    func draggedFiles() -> [DraggedFile] { files }
    func carriesPromises() -> Bool { promises }
}

/// A drag starter whose drag begins and ends when a test says.
@MainActor
final class FakeStarter: DragStarting {
    var hasBegun = false
    private(set) var prepared: (items: [DragItem], point: CGPoint)?
    private(set) var cancels = 0
    private var completion: ((DragOutcome) -> Void)?

    var isActive: Bool { completion != nil }

    func prepare(
        _ items: [DragItem], reader: any PromisedContentReading, at point: CGPoint,
        completion: @escaping (DragOutcome) -> Void
    ) {
        finish(.cancelled)
        prepared = (items, point)
        self.completion = completion
    }

    func cancel() {
        cancels += 1
        finish(.cancelled)
    }

    func finish(_ outcome: DragOutcome) {
        hasBegun = false
        let completion = self.completion
        self.completion = nil
        completion?(outcome)
    }
}

@MainActor
final class FakeEdge: EdgeReleasing {
    enum Call: Equatable {
        case show(CGPoint, accepting: Bool)
        case capture(CGPoint)
        case hide
    }

    private(set) var calls: [Call] = []
    private var dropped: (() -> Void)?
    private var completion: ((Result<CapturedDrag, Error>) -> Void)?

    func show(at point: CGPoint, accepting: Bool) { calls.append(.show(point, accepting: accepting)) }

    func capture(
        at point: CGPoint, dropped: @escaping () -> Void, completion: @escaping (Result<CapturedDrag, Error>) -> Void
    ) {
        calls.append(.capture(point))
        self.dropped = dropped
        self.completion = completion
    }

    func hide() { calls.append(.hide) }

    /// The drag is dropped on the capture and its files become readable.
    func capture(_ result: Result<CapturedDrag, Error>) {
        dropped?()
        completion?(result)
    }
}

extension FakeControlMesh: TransferDriving {
    var transferWatch: any TransferFollowing { NoTransfers() }

    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    ) async throws(CoreError) -> UInt64 {
        throw .notFound
    }

    func cancelTransfer(_ id: UInt64) async {}
}

/// Transfers that end as a test says, from a background queue as core's thread would.
final class FakeTransfers: TransferDriving, TransferFollowing, @unchecked Sendable {
    private let lock = NSLock()
    private var receivedItems: [TransferItem] = []
    private var cancelledIDs: [UInt64] = []
    private var finishers: [UInt64: @Sendable (Result<ReceivedItem, CoreError>) -> Void] = [:]
    /// How each transfer ends; nil: when it is cancelled.
    private let outcome: Result<ReceivedItem, CoreError>?

    init(outcome: Result<ReceivedItem, CoreError>?) {
        self.outcome = outcome
    }

    var received: [TransferItem] { lock.withLock { receivedItems } }
    var cancelled: [UInt64] { lock.withLock { cancelledIDs } }
    var transferWatch: any TransferFollowing { self }

    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    ) async throws(CoreError) -> UInt64 {
        lock.withLock {
            receivedItems.append(item)
            return UInt64(receivedItems.count)
        }
    }

    func cancelTransfer(_ id: UInt64) async {
        let finished = lock.withLock {
            cancelledIDs.append(id)
            return finishers.removeValue(forKey: id)
        }
        finished?(.failure(.cancelled))
    }

    func watch(
        _ id: UInt64, progress: @escaping @Sendable (TransferProgress) -> Void,
        finished: @escaping @Sendable (Result<ReceivedItem, CoreError>) -> Void
    ) {
        guard let outcome else {
            lock.withLock { finishers[id] = finished }
            return
        }
        DispatchQueue.global().async { finished(outcome) }
    }
}
