import CoreGraphics
import Foundation

/// Captures input with an active event tap at the HID level, on a thread of its own whose callback
/// only translates and enqueues (DEVPLAN, macOS constraints). Mode changes, the release chord and
/// the watchdog all go through one serial queue, so the cursor is hidden and shown exactly once per
/// capture. It is frozen where it crossed, and frozen again where it is whenever the tap sees it
/// follow the mouse (CursorFreeze). It is never warped while captured: macOS adds a warp's
/// distance to the next event's delta, which would throw the cursor on the other computer back.
final class EventTapCapture: InputCapturing {
    let events: AsyncStream<CaptureEvent>
    private let continuation: AsyncStream<CaptureEvent>.Continuation
    private let cursor: any CursorHiding
    private let captureGuard: any CaptureGuarding
    private let context: TapContext
    private let transitions = DispatchQueue(label: "com.akinalpfdn.osdrop.capture", qos: .userInteractive)
    private let running = Locked<RunningTap?>(nil)
    /// Held while the tap runs: App Nap would delay the watchdog and the heartbeats the capture
    /// guard depends on, and add latency to every captured event.
    private let activity = Locked<NSObjectProtocol?>(nil)

    init(cursor: any CursorHiding = BackgroundCursorHider(), captureGuard: any CaptureGuarding = ProcessCaptureGuard())
    {
        (events, continuation) = AsyncStream.makeStream(
            of: CaptureEvent.self, bufferingPolicy: .bufferingNewest(Tunables.Input.eventBufferLimit))
        self.cursor = cursor
        self.captureGuard = captureGuard
        context = TapContext(continuation: continuation)
        context.onReleaseChord = { [weak self] in
            guard let self else { return }
            transitions.async { [weak self] in
                self?.apply(.observe, reason: .releaseChord)
            }
        }
        context.onLoose = { [weak self] in
            guard let self else { return }
            transitions.async { [weak self] in
                self?.refreeze()
            }
        }
    }

    deinit {
        // No queued work can hold this object any more, so the release runs here directly; going
        // through `transitions` could deadlock when the last reference is dropped on that queue.
        apply(.observe, reason: .requested)
        stopThread()
        continuation.finish()
    }

    var mode: CaptureMode {
        context.state.withLock { $0.mode }
    }

    var isRunning: Bool {
        running.withLock { $0 != nil }
    }

    func start(layout: KeyboardPhysicalLayout) throws(CaptureError) {
        guard !isRunning else { return }
        guard captureGuard.ensureRunning() else { throw .guardUnavailable }
        context.state.withLock { $0.translator = EventTranslator(layout: layout) }
        let started = Locked<Result<CFRunLoop, CaptureError>?>(nil)
        let ready = DispatchSemaphore(value: 0)
        let finished = DispatchSemaphore(value: 0)
        let thread = Thread { [context] in
            Self.runTap(context: context, started: started, ready: ready)
            finished.signal()
        }
        thread.name = "Osdrop event tap"
        thread.qualityOfService = .userInteractive
        thread.start()

        let timeout = DispatchTime.now() + Tunables.Input.tapThreadStopTimeout.timeInterval
        guard ready.wait(timeout: timeout) == .success, let result = started.withLock({ $0 }) else {
            throw .threadDidNotStart
        }
        switch result {
        case .success(let runLoop):
            activity.withLock {
                $0 = ProcessInfo.processInfo.beginActivity(
                    options: [.userInitiatedAllowingIdleSystemSleep, .latencyCritical],
                    reason: "Osdrop watches this Mac's input")
            }
            let watchdog = makeWatchdog()
            running.withLock { $0 = RunningTap(runLoop: runLoop, finished: finished, watchdog: watchdog) }
            watchdog.resume()
        case .failure(let error):
            throw error
        }
    }

    func stop() {
        transitions.sync { apply(.observe, reason: .requested) }
        stopThread()
    }

    private func stopThread() {
        guard
            let tap = running.withLock({ state -> RunningTap? in
                defer { state = nil }
                return state
            })
        else {
            return
        }
        tap.watchdog.cancel()
        CFRunLoopStop(tap.runLoop)
        if let activity = activity.withLock({ state -> NSObjectProtocol? in
            defer { state = nil }
            return state
        }) {
            ProcessInfo.processInfo.endActivity(activity)
        }
        if tap.finished.wait(timeout: .now() + Tunables.Input.tapThreadStopTimeout.timeInterval) == .timedOut {
            Log.input.error("The event tap thread did not stop in time")
        }
    }

    func setMode(_ mode: CaptureMode) {
        transitions.sync { apply(mode, reason: .requested) }
    }

    func heartbeat() {
        context.state.withLock { $0.lastHeartbeat = .now }
    }

    func setHotkeys(_ hotkeys: CaptureHotkeys) {
        context.state.withLock { $0.hotkeys = hotkeys }
    }

    /// Runs on `transitions` only.
    private func apply(_ mode: CaptureMode, reason: CaptureReleaseReason) {
        guard context.state.withLock({ $0.mode }) != mode else { return }
        switch mode {
        case .capture:
            // Capture never starts without the guard that gives the cursor back if Osdrop hangs.
            guard captureGuard.ensureRunning() else {
                Log.input.error("Capture refused: the capture guard is not running")
                continuation.yield(.released(.guardUnavailable))
                return
            }
            captureGuard.send(.arm)
            context.state.withLock { state in
                state.mode = .capture
                state.lastHeartbeat = .now
                state.tapDisabledChecks = 0
                state.looseCount = 0
            }
            freeze()
            cursor.hide()
        case .observe:
            let looseCount = context.state.withLock { state -> Int in
                state.mode = .observe
                state.frozen = nil
                state.refreezing = false
                return state.looseCount
            }
            CGAssociateMouseAndMouseCursorPosition(1)
            cursor.show()
            captureGuard.send(.disarm)
            if looseCount > 1 {
                Log.input.error("The cursor followed the mouse \(looseCount, privacy: .public) times in one capture")
            }
            continuation.yield(.released(reason))
        }
        Log.input.info("Capture mode \(String(describing: mode), privacy: .public)")
    }

    /// Runs on `transitions` only. Disconnects the cursor from the mouse where it is and notes that
    /// place, which the tap compares every captured movement with.
    private func freeze() {
        CGAssociateMouseAndMouseCursorPosition(0)
        let location = CGEvent(source: nil)?.location
        context.state.withLock { state in
            state.frozen = location.map { FrozenCursor(point: $0, since: .now) }
            state.refreezing = false
        }
    }

    /// Runs on `transitions` only, after the tap saw the captured cursor follow the mouse: macOS
    /// connected them again, as it does when another app comes to the front. The cursor is frozen
    /// where it went, not warped back.
    private func refreeze() {
        let count = context.state.withLock { state -> Int? in
            guard state.mode == .capture, state.frozen != nil else {
                state.refreezing = false
                return nil
            }
            state.looseCount += 1
            return state.looseCount
        }
        guard let count else { return }
        if count == 1 {
            Log.input.error("The cursor followed the mouse during a capture; frozen again")
        }
        freeze()
    }

    private func makeWatchdog() -> DispatchSourceTimer {
        let timer = DispatchSource.makeTimerSource(queue: transitions)
        let interval = Tunables.Input.watchdogCheckInterval.timeInterval
        timer.schedule(deadline: .now() + interval, repeating: interval)
        timer.setEventHandler { [weak self] in
            self?.checkCapture()
        }
        return timer
    }

    /// Runs on `transitions`, on every watchdog tick. Ends a capture whose owner went quiet or whose
    /// event tap macOS turned off; otherwise tells the capture guard that all is well.
    private func checkCapture() {
        let (mode, isStale, isTapLost) = context.state.withLock { state -> (CaptureMode, Bool, Bool) in
            let isEnabled = state.port.map { CGEvent.tapIsEnabled(tap: $0) } ?? false
            state.tapDisabledChecks = isEnabled ? 0 : state.tapDisabledChecks + 1
            return (
                state.mode, ContinuousClock.now - state.lastHeartbeat > Tunables.Input.captureHeartbeatTimeout,
                state.tapDisabledChecks >= Tunables.Input.tapLostChecks
            )
        }
        guard mode == .capture else { return }
        if isStale {
            Log.input.error("Capture released by the watchdog")
            apply(.observe, reason: .watchdog)
        } else if isTapLost {
            Log.input.error("Capture released: the event tap stayed disabled")
            apply(.observe, reason: .tapLost)
        } else {
            captureGuard.send(.heartbeat)
        }
    }

    private static func runTap(
        context: TapContext, started: Locked<Result<CFRunLoop, CaptureError>?>, ready: DispatchSemaphore
    ) {
        let mask = CGEventMask.max
        guard
            let port = CGEvent.tapCreate(
                tap: .cghidEventTap, place: .headInsertEventTap, options: .defaultTap, eventsOfInterest: mask,
                callback: { _, type, event, userInfo in
                    guard let userInfo else { return Unmanaged.passUnretained(event) }
                    return Unmanaged<TapContext>.fromOpaque(userInfo).takeUnretainedValue().handle(type, event)
                }, userInfo: Unmanaged.passUnretained(context).toOpaque())
        else {
            started.withLock { $0 = .failure(.tapUnavailable) }
            ready.signal()
            return
        }
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, port, 0)
        let runLoop = CFRunLoopGetCurrent()
        CFRunLoopAddSource(runLoop, source, .commonModes)
        context.state.withLock { $0.port = port }
        CGEvent.tapEnable(tap: port, enable: true)
        started.withLock { $0 = runLoop.map { .success($0) } ?? .failure(.threadDidNotStart) }
        ready.signal()

        CFRunLoopRun()

        CGEvent.tapEnable(tap: port, enable: false)
        CFRunLoopRemoveSource(runLoop, source, .commonModes)
        CFMachPortInvalidate(port)
        context.state.withLock { $0.port = nil }
    }
}

private struct RunningTap {
    let runLoop: CFRunLoop
    let finished: DispatchSemaphore
    let watchdog: DispatchSourceTimer
}

/// Where a captured cursor waits, and since when.
private struct FrozenCursor {
    var point: CGPoint
    var since: ContinuousClock.Instant
}

/// State shared by the tap callback (tap thread) and the capture's queue, behind one lock.
private final class TapContext: @unchecked Sendable {
    struct State {
        var mode: CaptureMode = .observe
        var translator = EventTranslator(layout: .ansi)
        var port: CFMachPort?
        var lastHeartbeat = ContinuousClock.now
        /// Consecutive watchdog checks that found the tap disabled.
        var tapDisabledChecks = 0
        var hotkeys = CaptureHotkeys()
        /// While captured, once the cursor is frozen: where it waits.
        var frozen: FrozenCursor?
        /// The cursor was seen following the mouse and is being frozen again.
        var refreezing = false
        /// How often the cursor followed the mouse in this capture.
        var looseCount = 0
    }

    let state = Locked(State())
    let continuation: AsyncStream<CaptureEvent>.Continuation
    var onReleaseChord: (@Sendable () -> Void)?
    var onLoose: (@Sendable () -> Void)?

    init(continuation: AsyncStream<CaptureEvent>.Continuation) {
        self.continuation = continuation
    }

    /// The tap callback. It must return quickly: macOS disables a tap whose callback is slow.
    func handle(_ type: CGEventType, _ event: CGEvent) -> Unmanaged<CGEvent>? {
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let port = state.withLock({ $0.port }) {
                CGEvent.tapEnable(tap: port, enable: true)
            }
            continuation.yield(.tapReenabled(type == .tapDisabledByTimeout ? .timeout : .userInput))
            return Unmanaged.passUnretained(event)
        }
        let isInjected = event.getIntegerValueField(.eventSourceUserData) == Tunables.Input.injectedEventTag
        let movesCursor = Self.cursorMoves.contains(type)
        if isInjected {
            if movesCursor {
                // Osdrop moved the cursor itself (a drag let go at the edge): it waits there now.
                let location = event.location
                state.withLock { state in
                    if state.mode == .capture, state.frozen != nil {
                        state.frozen = FrozenCursor(point: location, since: .now)
                    }
                }
            }
            return Unmanaged.passUnretained(event)
        }
        let keyCode = event.getIntegerValueField(.keyboardEventKeycode)
        let hotkeys = state.withLock { $0.hotkeys }
        if hotkeys.lockInput.matches(type: type, keyCode: keyCode, flags: event.flags) {
            if event.getIntegerValueField(.keyboardEventAutorepeat) == 0 {
                continuation.yield(.hotkey(.lockInput))
            }
            return nil
        }
        let isReleaseChord = hotkeys.returnHome.matches(type: type, keyCode: keyCode, flags: event.flags)
        let location = event.location
        let now = ContinuousClock.now
        let (mode, inputs, isLoose) = state.withLock { state -> (CaptureMode, [InputEvent], Bool) in
            // The release chord belongs to this Mac; it is never reported as input.
            if state.mode == .capture, isReleaseChord {
                return (state.mode, [], false)
            }
            var isLoose = false
            if state.mode == .capture, movesCursor, !state.refreezing, let frozen = state.frozen,
                now - frozen.since >= Tunables.Input.freezeSettleTime,
                CursorFreeze.isLoose(location: location, frozen: frozen.point)
            {
                state.refreezing = true
                isLoose = true
            }
            return (state.mode, state.translator.translate(event), isLoose)
        }
        if mode == .capture, isReleaseChord {
            onReleaseChord?()
            return nil
        }
        if isLoose {
            onLoose?()
        }
        let tapTime = DispatchTime.now().uptimeNanoseconds
        for input in inputs {
            continuation.yield(
                .input(
                    CapturedInput(
                        event: input, timestamp: event.timestamp, location: event.location, tapTime: tapTime)))
        }
        return TapPolicy.decide(mode: mode, isInjected: false) == .swallow ? nil : Unmanaged.passUnretained(event)
    }

    /// The events whose location is where the cursor is.
    private static let cursorMoves: Set<CGEventType> = [
        .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged,
    ]
}

extension Duration {
    var timeInterval: TimeInterval {
        let (seconds, attoseconds) = components
        return TimeInterval(seconds) + TimeInterval(attoseconds) / 1e18
    }
}
