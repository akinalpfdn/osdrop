import CoreBridge
import CoreGraphics
import Foundation

/// Drag and drop between this Mac and the other computers, as input control hands it over.
@MainActor
protocol DragCoordinating: AnyObject {
    /// Watches for drags on this Mac while a member is connected.
    func start()
    func stop()
    /// A drag came with the cursor: it starts here, from a press at `point`.
    func arrived(source: DeviceID, dragID: UInt32, at point: CGPoint, items: [ArrivingItem]) async
    func withdrawn(_ dragID: UInt32)
    /// This Mac's drag waits at the edge: release it there.
    func ended(_ dragID: UInt32, _ outcome: DropOutcome)
    /// This Mac's drag of promised files is wanted elsewhere: take its files at the edge.
    func wanted()
}

/// Starts drags with an origin under a point and reports how they ended (`PromiseDragStarter`).
@MainActor
protocol DragStarting: AnyObject {
    /// A drag is prepared or running.
    var isActive: Bool { get }
    /// The press arrived and the drag runs.
    var hasBegun: Bool { get }
    func prepare(
        _ items: [DragItem], reader: any PromisedContentReading, at point: CGPoint,
        completion: @escaping (DragOutcome) -> Void)
    func cancel()
}

/// The pauses of a drag handed over between computers.
struct DragTiming: Sendable {
    /// The window a drag from another computer starts from shows for this long before the press
    /// reaches it; the drag is given up when it has not begun `beginTimeout` after.
    var pressDelay = Tunables.Drag.pressDelay
    var beginTimeout = Tunables.Drag.beginTimeout
    /// This Mac's drag waiting at the edge: its drop target shows this long before the release
    /// and stays this long after.
    var releaseSettle = Tunables.Drag.releaseSettle
    var releaseHold = Tunables.Drag.releaseHold
}

/// The Mac's part of drags across computers (DEVPLAN Phase 14; docs/PROTOCOL.md, "Drag messages").
/// Core decides where a drag goes; this controller reports the drags that start on this Mac, starts
/// the ones that arrive with promises fetched from their source, and ends this Mac's own drag at
/// the edge once its outcome is known.
@MainActor
final class DragController: DragCoordinating {
    /// Escape cancels a drag in progress.
    static let escapeUsage: UInt16 = 0x29

    private let mesh: any ControlDriving & TransferDriving
    private let injector: any InputInjecting
    private let staging: DragStaging
    private let watcher: any DragWatching
    private let starter: any DragStarting
    private let edge: any EdgeReleasing
    private let cursorLocation: @MainActor () -> CGPoint
    private let timing: DragTiming
    private let transfers: (any DragTransfersTracking)?
    /// The drag that came here and is shown, until it ends.
    private var arriving: UInt32?
    /// Drags that never began: reported as failed.
    private var unstarted: Set<UInt32> = []
    private var release: Task<Void, Never>?
    /// The files of this Mac's drag of promised files being taken at the edge, by attempt.
    private var taking: UInt64?
    private var takingDropped = false
    private var attempts: UInt64 = 0
    /// Folders of promised files that were carried away, oldest first: kept while other
    /// computers may still fetch them.
    private var staged: [URL] = []

    init(
        mesh: any ControlDriving & TransferDriving, injector: any InputInjecting, staging: DragStaging,
        watcher: any DragWatching,
        starter: any DragStarting, edge: any EdgeReleasing, cursorLocation: @escaping @MainActor () -> CGPoint,
        timing: DragTiming = DragTiming(), transfers: (any DragTransfersTracking)? = nil
    ) {
        self.mesh = mesh
        self.injector = injector
        self.staging = staging
        self.watcher = watcher
        self.starter = starter
        self.edge = edge
        self.cursorLocation = cursorLocation
        self.timing = timing
        self.transfers = transfers
    }

    func start() {
        watcher.onChange = { [weak self] dragging in
            self?.localDragChanged(dragging)
        }
        watcher.start()
    }

    func stop() {
        watcher.stop()
        if let arriving {
            withdrawn(arriving)
        }
        release?.cancel()
        edge.hide()
    }

    // MARK: - Drags on this Mac

    private func localDragChanged(_ dragging: Bool) {
        // Osdrop's own drag, or the drag Osdrop ends at the edge for its files.
        guard arriving == nil, !starter.isActive, taking == nil else { return }
        guard dragging else {
            mesh.dragStopped()
            return
        }
        // Promised files come first: Photos also puts the URL of a small preview on the
        // pasteboard, beside its promise of the photo itself.
        if watcher.carriesPromises() {
            mesh.dragPromised()
        } else {
            offer(watcher.draggedFiles())
        }
    }

    private func offer(_ files: [DraggedFile]) {
        if let dragID = mesh.dragStarted(files) {
            transfers?.offered(dragID: dragID, names: files.map(\.url.lastPathComponent))
        }
    }

    func wanted() {
        let point = cursorLocation()
        attempts += 1
        let attempt = attempts
        taking = attempt
        takingDropped = false
        Log.drag.info("Taking the files of this Mac's drag at the edge")
        edge.capture(
            at: point, dropped: { [weak self] in self?.takingDropped = true },
            completion: { [weak self] result in self?.took(attempt, result) })
        release?.cancel()
        release = Task { [weak self, timing] in
            try? await Task.sleep(for: timing.releaseSettle)
            guard let self, !Task.isCancelled else { return }
            injector.releaseDrag(at: point)
            try? await Task.sleep(for: timing.releaseHold)
            if taking == attempt, !takingDropped {
                took(attempt, .failure(CocoaError(.userCancelled)))  // the drop did not reach it
            }
        }
    }

    private func took(_ attempt: UInt64, _ result: Result<CapturedDrag, Error>) {
        guard taking == attempt else { return }  // a later attempt replaced it
        taking = nil
        edge.hide()
        switch result {
        case .success(let drag):
            if let folder = drag.stagingFolder {
                keep(folder)
            }
            offer(
                drag.items.compactMap { item in
                    drag.sources[item.id].map { DraggedFile(url: $0, contentType: item.contentType) }
                })
        case .failure(let error):
            Log.drag.error("Taking a drag's files failed: \(error.localizedDescription, privacy: .public)")
            mesh.dragStarted([])
        }
    }

    private func keep(_ folder: URL) {
        staged.append(folder)
        while staged.count > Tunables.Drag.keptStagings {
            staging.remove(staged.removeFirst())
        }
    }

    func ended(_ dragID: UInt32, _ outcome: DropOutcome) {
        let point = cursorLocation()
        Log.drag.info("Ending this Mac's drag at the edge: \(String(describing: outcome), privacy: .public)")
        edge.show(at: point, accepting: outcome == .dropped)
        release?.cancel()
        release = Task { [weak self, timing] in
            // The drop target shows before the drag is released over it.
            try? await Task.sleep(for: timing.releaseSettle)
            guard let self, !Task.isCancelled else { return }
            injector.releaseDrag(at: point)
            try? await Task.sleep(for: timing.releaseHold)
            guard !Task.isCancelled else { return }
            edge.hide()
        }
    }

    // MARK: - Drags from other computers

    func arrived(source: DeviceID, dragID: UInt32, at point: CGPoint, items: [ArrivingItem]) async {
        let dragItems = items.map {
            DragItem(
                id: UUID(), name: $0.name, isDirectory: $0.isFolder, byteCount: $0.byteCount,
                contentType: $0.contentType)
        }
        let indices = Dictionary(uniqueKeysWithValues: dragItems.enumerated().map { ($1.id, UInt32($0)) })
        let reader = NetworkContentReader(
            fetcher: MeshDragFetcher(mesh: mesh, source: source, dragID: dragID), indices: indices,
            started: { [transfers] item, transfer in
                // In order with the start's other work on the main thread.
                DispatchQueue.main.async {
                    MainActor.assumeIsolated {
                        transfers?.receiving(transfer, name: item.name, from: source, bytes: item.byteCount)
                    }
                }
            })
        Log.drag.info("A drag of \(items.count, privacy: .public) items arrived")
        arriving = dragID
        starter.prepare(dragItems, reader: reader, at: point) { [weak self] outcome in
            self?.arrivalEnded(dragID, outcome)
        }
        // The window the drag starts from shows before the press reaches it.
        try? await Task.sleep(for: timing.pressDelay)
        guard arriving == dragID else { return }
        injector.button(.left, isDown: true, at: point)
        Task { [weak self, timing] in
            try? await Task.sleep(for: timing.beginTimeout)
            self?.checkBegun(dragID)
        }
    }

    private func checkBegun(_ dragID: UInt32) {
        guard arriving == dragID, !starter.hasBegun else { return }
        Log.drag.error("A drag that arrived did not start")
        unstarted.insert(dragID)
        starter.cancel()
    }

    func withdrawn(_ dragID: UInt32) {
        guard arriving == dragID else { return }
        arriving = nil
        if starter.hasBegun {
            injector.key(.keyboard(Self.escapeUsage), .down)
            injector.key(.keyboard(Self.escapeUsage), .up)
        } else {
            starter.cancel()
        }
    }

    private func arrivalEnded(_ dragID: UInt32, _ outcome: DragOutcome) {
        if arriving == dragID {
            arriving = nil
        }
        let reported: DropOutcome =
            if unstarted.remove(dragID) != nil {
                .failed
            } else {
                outcome == .dropped ? .dropped : .cancelled
            }
        mesh.dropFinished(dragID, reported)
    }
}
