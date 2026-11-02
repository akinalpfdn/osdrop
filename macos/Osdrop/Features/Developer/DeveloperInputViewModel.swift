#if DEBUG
    import CoreGraphics
    import Foundation
    import Observation

    /// The debug-only developer window of Phase 1: takes over this Mac's input for a few seconds,
    /// then plays back what was recorded, so capture and injection are checked on real hardware.
    @MainActor
    @Observable
    final class DeveloperInputViewModel {
        enum TapState: Equatable {
            case stopped
            case running
            case failed(CaptureError)
        }

        enum RunPhase: Equatable {
            case idle
            /// Time to put the visible cursor where the test happens; capture hides it.
            case startingIn(secondsLeft: Int)
            case capturing(secondsLeft: Int)
            case replaying
            /// `replayedEvents` is nil when replay was off.
            case finished(recordedEvents: Int, replayedEvents: Int?, release: CaptureReleaseReason)
        }

        struct Counts: Equatable {
            var pointer = 0
            var buttons = 0
            var scroll = 0
            var keys = 0
        }

        let accessibility: AccessibilityPermission
        let hidesCursorInBackground: Bool
        var captureSeconds = Tunables.Developer.defaultCaptureSeconds
        /// Off by default: replayed keys go to whichever app has focus once the capture ends.
        var replaysAfterCapture = false
        private(set) var tapState: TapState = .stopped
        private(set) var phase: RunPhase = .idle
        private(set) var counts = Counts()
        private(set) var tapReenableCount = 0
        private(set) var lastKey: HIDKey?
        private(set) var droppedKeyCount = 0
        private(set) var focusAcquired: Bool?
        private(set) var secureInput: SecureInputStatus = .off
        private(set) var screens: [ScreenDescription] = []
        private(set) var layout: KeyboardPhysicalLayout

        @ObservationIgnored private let capture: any InputCapturing
        @ObservationIgnored private let injector: any InputInjecting
        @ObservationIgnored private let focus: any CaptureFocusing
        @ObservationIgnored private let secureInputChecker: any SecureInputChecking
        @ObservationIgnored private let screenDescriber: any ScreenDescribing
        @ObservationIgnored private var recording: [CapturedInput]?
        @ObservationIgnored private var captureRelease: CaptureReleaseReason?
        @ObservationIgnored nonisolated(unsafe) private var eventsTask: Task<Void, Never>?
        @ObservationIgnored nonisolated(unsafe) private var runTask: Task<Void, Never>?

        init(dependencies: AppDependencies) {
            accessibility = dependencies.accessibility
            hidesCursorInBackground = dependencies.cursor.hidesInBackground
            capture = dependencies.capture
            injector = dependencies.injector
            focus = dependencies.focus
            secureInputChecker = dependencies.secureInput
            screenDescriber = dependencies.screens
            layout = KeyboardPhysicalLayout.current()
            screens = screenDescriber.screens()
            screenDescriber.onChange = { [weak self] in
                guard let self else { return }
                screens = screenDescriber.screens()
            }
        }

        deinit {
            eventsTask?.cancel()
            runTask?.cancel()
        }

        var canCapture: Bool {
            tapState == .running && !isBusy
        }

        var isBusy: Bool {
            switch phase {
            case .startingIn, .capturing, .replaying: true
            case .idle, .finished: false
            }
        }

        func refresh() {
            accessibility.refresh()
            secureInput = secureInputChecker.current()
            layout = KeyboardPhysicalLayout.current()
            droppedKeyCount = injector.droppedKeyCount
        }

        func startTap() {
            layout = KeyboardPhysicalLayout.current()
            do {
                try capture.start(layout: layout)
                tapState = .running
                listen()
            } catch {
                tapState = .failed(error)
            }
        }

        func stopTap() {
            runTask?.cancel()
            capture.stop()
            tapState = .stopped
        }

        func captureAndReplay() {
            guard canCapture else { return }
            runTask = Task { [weak self] in
                await self?.runCaptureAndReplay()
            }
        }

        private func listen() {
            guard eventsTask == nil else { return }
            let events = capture.events
            eventsTask = Task { [weak self] in
                for await event in events {
                    self?.handle(event)
                }
            }
        }

        private func handle(_ event: CaptureEvent) {
            switch event {
            case .input(let input):
                count(input.event)
                recording?.append(input)
            case .tapReenabled:
                tapReenableCount += 1
            case .released(let reason):
                if recording != nil {
                    captureRelease = reason
                }
            }
        }

        private func count(_ event: InputEvent) {
            switch event {
            case .pointerMoved: counts.pointer += 1
            case .button: counts.buttons += 1
            case .scroll: counts.scroll += 1
            case .key(let key, _):
                counts.keys += 1
                lastKey = key
            }
        }

        private func runCaptureAndReplay() async {
            for secondsLeft in stride(from: Tunables.Developer.captureCountdownSeconds, to: 0, by: -1) {
                phase = .startingIn(secondsLeft: secondsLeft)
                // A cancelled sleep ends the countdown through Task.isCancelled.
                try? await Task.sleep(for: .seconds(1))
                guard !Task.isCancelled else {
                    phase = .idle
                    return
                }
            }
            let start = CGEventInjector.currentCursorLocation()
            recording = []
            captureRelease = nil
            focus.acquire()
            capture.setMode(.capture)
            secureInput = secureInputChecker.current()

            let deadline = ContinuousClock.now + .seconds(captureSeconds)
            // Losing Accessibility access ends the capture like any other way out.
            while ContinuousClock.now < deadline, captureRelease == nil, accessibility.isTrusted, !Task.isCancelled {
                capture.heartbeat()
                let left = (deadline - ContinuousClock.now).timeInterval
                phase = .capturing(secondsLeft: Int(left.rounded(.up)))
                // A cancelled sleep ends the loop through Task.isCancelled.
                try? await Task.sleep(for: Tunables.Input.captureHeartbeatInterval)
                focusAcquired = focus.hasFocus
                secureInput = secureInputChecker.current()
            }
            capture.setMode(.observe)
            focus.release()
            let release = captureRelease ?? .requested
            let recorded = recording ?? []
            recording = nil

            let replayed = replaysAfterCapture ? await replay(recorded, from: start) : nil
            phase = .finished(recordedEvents: recorded.count, replayedEvents: replayed, release: release)
        }

        private func replay(_ recorded: [CapturedInput], from start: CGPoint) async -> Int {
            phase = .replaying
            let bounds = screens.map(\.frame).reduce(CGRect.null) { $0.union($1) }
            let plan = ReplayPlan(
                recording: recorded, start: start, bounds: bounds, maximumGap: Tunables.Developer.maximumReplayGap)
            for step in plan.steps where !Task.isCancelled {
                // A cancelled sleep stops the replay through Task.isCancelled.
                try? await Task.sleep(for: step.delay)
                apply(step.action)
            }
            injector.releaseAll()
            droppedKeyCount = injector.droppedKeyCount
            return plan.steps.count
        }

        private func apply(_ action: ReplayPlan.Action) {
            switch action {
            case .move(let point): injector.moveCursor(to: point)
            case .button(let button, let isDown, let point): injector.button(button, isDown: isDown, at: point)
            case .scroll(let delta): injector.scroll(delta)
            case .key(let key, let keyAction): injector.key(key, keyAction)
            }
        }
    }
#endif
