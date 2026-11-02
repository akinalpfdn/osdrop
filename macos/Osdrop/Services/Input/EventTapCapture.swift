import CoreGraphics
import Foundation

/// Captures input with an active event tap at the HID level, on a thread of its own whose callback
/// only translates and enqueues (DEVPLAN, macOS constraints). Mode changes, the release chord and
/// the watchdog all go through one serial queue, so the cursor is frozen, hidden, unfrozen and
/// shown exactly once per capture.
final class EventTapCapture: InputCapturing {
    let events: AsyncStream<CaptureEvent>
    private let continuation: AsyncStream<CaptureEvent>.Continuation
    private let cursor: any CursorHiding
    private let context: TapContext
    private let transitions = DispatchQueue(label: "com.akinalpfdn.osdrop.capture", qos: .userInteractive)
    private let running = Locked<RunningTap?>(nil)

    init(cursor: any CursorHiding = BackgroundCursorHider()) {
        (events, continuation) = AsyncStream.makeStream(
            of: CaptureEvent.self, bufferingPolicy: .bufferingNewest(Tunables.Input.eventBufferLimit))
        self.cursor = cursor
        context = TapContext(continuation: continuation)
        context.onReleaseChord = { [weak self] in
            guard let self else { return }
            transitions.async { [weak self] in
                self?.apply(.observe, reason: .releaseChord)
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

    /// Runs on `transitions` only.
    private func apply(_ mode: CaptureMode, reason: CaptureReleaseReason) {
        let previous = context.state.withLock { state -> CaptureMode in
            defer {
                state.mode = mode
                state.lastHeartbeat = .now
            }
            return state.mode
        }
        guard previous != mode else { return }
        switch mode {
        case .capture:
            CGAssociateMouseAndMouseCursorPosition(0)
            cursor.hide()
        case .observe:
            CGAssociateMouseAndMouseCursorPosition(1)
            cursor.show()
            continuation.yield(.released(reason))
        }
        Log.input.info("Capture mode \(String(describing: mode), privacy: .public)")
    }

    private func makeWatchdog() -> DispatchSourceTimer {
        let timer = DispatchSource.makeTimerSource(queue: transitions)
        let interval = Tunables.Input.watchdogCheckInterval.timeInterval
        timer.schedule(deadline: .now() + interval, repeating: interval)
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            let isStale = context.state.withLock { state in
                state.mode == .capture
                    && ContinuousClock.now - state.lastHeartbeat > Tunables.Input.captureHeartbeatTimeout
            }
            if isStale {
                Log.input.error("Capture released by the watchdog")
                apply(.observe, reason: .watchdog)
            }
        }
        return timer
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

/// State shared by the tap callback (tap thread) and the capture's queue, behind one lock.
private final class TapContext: @unchecked Sendable {
    struct State {
        var mode: CaptureMode = .observe
        var translator = EventTranslator(layout: .ansi)
        var port: CFMachPort?
        var lastHeartbeat = ContinuousClock.now
    }

    let state = Locked(State())
    let continuation: AsyncStream<CaptureEvent>.Continuation
    var onReleaseChord: (@Sendable () -> Void)?

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
        if isInjected {
            return Unmanaged.passUnretained(event)
        }
        let keyCode = event.getIntegerValueField(.keyboardEventKeycode)
        let isReleaseChord = TapPolicy.isReleaseChord(type: type, keyCode: keyCode, flags: event.flags)
        let (mode, inputs) = state.withLock { state -> (CaptureMode, [InputEvent]) in
            // The release chord belongs to this Mac; it is never reported as input.
            if state.mode == .capture, isReleaseChord {
                return (state.mode, [])
            }
            return (state.mode, state.translator.translate(event))
        }
        if mode == .capture, isReleaseChord {
            onReleaseChord?()
            return nil
        }
        for input in inputs {
            continuation.yield(.input(CapturedInput(event: input, timestamp: event.timestamp)))
        }
        return TapPolicy.decide(mode: mode, isInjected: false) == .swallow ? nil : Unmanaged.passUnretained(event)
    }
}

extension Duration {
    var timeInterval: TimeInterval {
        let (seconds, attoseconds) = components
        return TimeInterval(seconds) + TimeInterval(attoseconds) / 1e18
    }
}
