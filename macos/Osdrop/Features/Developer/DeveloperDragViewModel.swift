#if DEBUG
    import AppKit
    import Observation

    /// The debug-only drag loopback of Phase 2: a drag dropped on the right edge of the main display
    /// is captured, then Osdrop drags the same items from the left edge, with injected input, to
    /// wherever the user put the cursor. On one Mac this has to be two steps: macOS runs one drag at a
    /// time, and the user's own drag holds the button.
    @MainActor
    @Observable
    final class DeveloperDragViewModel {
        enum Phase: Equatable {
            case idle
            case armed
            case captured(CapturedDrag)
            case startingIn(secondsLeft: Int)
            case dragging
            case finished(DragOutcome)
            case failed(Failure)
        }

        enum Failure: Equatable {
            case captureFailed
            case tapUnavailable
        }

        /// When off, drops on the edge are refused and the drag slides back.
        var acceptsDrops = true
        private(set) var phase: Phase = .idle
        private(set) var enteredFiles = 0
        private(set) var enteredPromises = 0
        private(set) var writtenItems = 0
        private(set) var failedItems = 0

        @ObservationIgnored private let capture: any InputCapturing
        @ObservationIgnored private let injector: any InputInjecting
        @ObservationIgnored private let staging: DragStaging
        @ObservationIgnored private let monitor = DragActivityMonitor()
        @ObservationIgnored private let edge: EdgeDropTarget
        @ObservationIgnored private let starter: PromiseDragStarter
        @ObservationIgnored private var activity: NSObjectProtocol?
        @ObservationIgnored nonisolated(unsafe) private var runTask: Task<Void, Never>?

        init(dependencies: AppDependencies) {
            capture = dependencies.capture
            injector = dependencies.injector
            staging = dependencies.staging
            edge = EdgeDropTarget(staging: dependencies.staging, highlighted: true)
            starter = PromiseDragStarter(staging: dependencies.staging)
            monitor.onChange = { [weak self] dragging in
                self?.dragActivityChanged(dragging)
            }
            edge.onEnter = { [weak self] files, promises in
                self?.enteredFiles = files
                self?.enteredPromises = promises
            }
            edge.decide = { [weak self] in
                self?.acceptsDrops == true ? .acceptAsCopy : .reject
            }
            edge.onCapture = { [weak self] result in
                self?.captured(result)
            }
            starter.onItemWritten = { [weak self] _, result in
                switch result {
                case .success: self?.writtenItems += 1
                case .failure: self?.failedItems += 1
                }
            }
        }

        deinit {
            runTask?.cancel()
        }

        var isArmed: Bool {
            phase != .idle
        }

        var canDrop: Bool {
            if case .captured = phase { true } else { false }
        }

        func arm() {
            guard phase == .idle else { return }
            // The monitor's timer must keep its pace while Osdrop is in the background.
            activity = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiatedAllowingIdleSystemSleep, .latencyCritical], reason: "Drag loopback")
            monitor.start()
            phase = .armed
        }

        func disarm() {
            runTask?.cancel()
            monitor.stop()
            edge.hide()
            starter.cancel()
            removeStaging()
            if let activity {
                ProcessInfo.processInfo.endActivity(activity)
            }
            activity = nil
            phase = .idle
        }

        func dropWithOsdrop() {
            guard case .captured(let drag) = phase else { return }
            runTask = Task { [weak self] in
                await self?.run(drag)
            }
        }

        private func dragActivityChanged(_ dragging: Bool) {
            guard isArmed, !isInjecting else { return }
            if dragging, let screen = NSScreen.screens.first {
                let frame = screen.frame
                edge.show(
                    over: NSRect(
                        x: frame.maxX - Theme.Drag.edgeWidth, y: frame.minY, width: Theme.Drag.edgeWidth,
                        height: frame.height))
            } else {
                edge.hide()
            }
        }

        private var isInjecting: Bool {
            switch phase {
            case .startingIn, .dragging: true
            default: false
            }
        }

        private func captured(_ result: Result<CapturedDrag, Error>) {
            edge.hide()
            removeStaging()
            switch result {
            case .success(let drag): phase = .captured(drag)
            case .failure: phase = .failed(.captureFailed)
            }
        }

        private func removeStaging() {
            if case .captured(let drag) = phase, let folder = drag.stagingFolder {
                staging.remove(folder)
            }
        }

        private func run(_ drag: CapturedDrag) async {
            for secondsLeft in stride(from: Tunables.Developer.captureCountdownSeconds, to: 0, by: -1) {
                phase = .startingIn(secondsLeft: secondsLeft)
                // A cancelled sleep ends the run through Task.isCancelled.
                try? await Task.sleep(for: .seconds(1))
                guard !Task.isCancelled else { return }
            }
            let destination = CGEventInjector.currentCursorLocation()
            guard let screen = NSScreen.screens.first else { return }
            // The primary display's left edge, at the destination's height (global coordinates share
            // the primary display's origin).
            let origin = CGPoint(x: screen.frame.minX + Tunables.Developer.dragOriginInset, y: destination.y)
            if !capture.isRunning {
                do {
                    try capture.start(layout: KeyboardPhysicalLayout.current())
                } catch {
                    phase = .failed(.tapUnavailable)
                    return
                }
            }
            writtenItems = 0
            failedItems = 0
            phase = .dragging
            let outcome = await inject(drag, from: origin, to: destination)
            phase = .finished(outcome)
        }

        /// Takes over the physical input, drags from `origin` to `destination` with injected events,
        /// and always gives the input back.
        private func inject(_ drag: CapturedDrag, from origin: CGPoint, to destination: CGPoint) async -> DragOutcome {
            let ended = AsyncStream.makeStream(of: DragOutcome.self)
            starter.prepare(drag.items, reader: LocalFileContentReader(sources: drag.sources), at: origin) { outcome in
                ended.continuation.yield(outcome)
                ended.continuation.finish()
            }
            capture.setMode(.capture)
            defer {
                injector.releaseAll()
                capture.setMode(.observe)
            }
            injector.moveCursor(to: origin)
            await pause(Tunables.Developer.dragStepDelay)
            injector.button(.left, isDown: true, at: origin)
            let steps = Tunables.Developer.dragSteps
            for step in 1...steps {
                let fraction = Double(step) / Double(steps)
                injector.moveCursor(
                    to: CGPoint(
                        x: origin.x + (destination.x - origin.x) * fraction,
                        y: origin.y + (destination.y - origin.y) * fraction))
                await pause(Tunables.Developer.dragStepDelay)
            }
            // Drop targets update their highlight on movement; a short hover lets them settle.
            for wiggle in [1.0, -1.0, 0.0] {
                injector.moveCursor(to: CGPoint(x: destination.x + wiggle, y: destination.y))
                await pause(Tunables.Developer.dropHover)
            }
            injector.button(.left, isDown: false, at: destination)
            let outcome = await first(of: ended.stream, timeout: Tunables.Developer.dragEndTimeout)
            if outcome == nil {
                starter.cancel()
            }
            return outcome ?? .cancelled
        }

        private func pause(_ duration: Duration) async {
            capture.heartbeat()
            // A cancelled sleep only shortens the pause; the drag still ends normally.
            try? await Task.sleep(for: duration)
            capture.heartbeat()
        }

        private func first(of stream: AsyncStream<DragOutcome>, timeout: Duration) async -> DragOutcome? {
            let deadline = ContinuousClock.now + timeout
            let poll = Task { @MainActor [capture] in
                while ContinuousClock.now < deadline, !Task.isCancelled {
                    capture.heartbeat()
                    // Cancelled once the outcome is known.
                    try? await Task.sleep(for: Tunables.Input.captureHeartbeatInterval)
                }
            }
            defer { poll.cancel() }
            return await withTaskGroup(of: DragOutcome?.self) { group in
                group.addTask {
                    for await outcome in stream {
                        return outcome
                    }
                    return nil
                }
                group.addTask {
                    // Cancelled when the drag ends first.
                    try? await Task.sleep(for: timeout)
                    return nil
                }
                let result = await group.next() ?? nil
                group.cancelAll()
                return result
            }
        }
    }
#endif
