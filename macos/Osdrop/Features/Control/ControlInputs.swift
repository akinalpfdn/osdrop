import CoreBridge
import CoreGraphics
import Foundation

/// Core's positions are 1/16 point of a computer's native desktop; on the Mac that desktop is
/// CoreGraphics' global display space in points (top left origin), as the screen reports use it.
enum ControlCoordinates {
    static let scale = 16.0

    static func coord(_ point: CGPoint) -> CoordPoint {
        CoordPoint(x: coord(point.x), y: coord(point.y))
    }

    static func point(_ coord: CoordPoint) -> CGPoint {
        CGPoint(x: Double(coord.x) / scale, y: Double(coord.y) / scale)
    }

    private static func coord(_ value: Double) -> Int32 {
        Int32(clamping: Int64((value * scale).rounded()))
    }
}

/// Scroll amounts travel in 1/256 of a line or point; the protocol codes of phases and units.
/// macOS values pass unchanged in both directions until scroll normalisation (Phase 12).
enum ControlScroll {
    static let scale = 256.0

    static func stroke(_ delta: ScrollDelta) -> ScrollStroke {
        ScrollStroke(
            unit: delta.unit == .line ? 1 : 2, phase: code(delta.phase), momentum: code(delta.momentum),
            dx: Int32(clamping: Int64((delta.x * scale).rounded())),
            dy: Int32(clamping: Int64((delta.y * scale).rounded())))
    }

    static func delta(_ stroke: ScrollStroke) -> ScrollDelta {
        ScrollDelta(
            x: Double(stroke.dx) / scale, y: Double(stroke.dy) / scale, unit: stroke.unit == 1 ? .line : .pixel,
            phase: phase(stroke.phase), momentum: momentum(stroke.momentum))
    }

    private static let phases: [ScrollPhase] = [.none, .mayBegin, .began, .changed, .ended, .cancelled]
    private static let momentums: [MomentumPhase] = [.none, .began, .changed, .ended]

    private static func code(_ phase: ScrollPhase) -> UInt8 {
        UInt8(phases.firstIndex(of: phase) ?? 0)
    }

    private static func code(_ momentum: MomentumPhase) -> UInt8 {
        UInt8(momentums.firstIndex(of: momentum) ?? 0)
    }

    private static func phase(_ code: UInt8) -> ScrollPhase {
        Int(code) < phases.count ? phases[Int(code)] : .none
    }

    private static func momentum(_ code: UInt8) -> MomentumPhase {
        Int(code) < momentums.count ? momentums[Int(code)] : .none
    }
}

/// Turns this Mac's captured input into core's events, counting clicks with the Mac's own
/// double-click settings (docs/PROTOCOL.md "0x24 ButtonEvent"). Runs off the main thread.
struct InputForwarder {
    /// The highest button number the protocol carries.
    static let maxButton = 31

    private var clicks: ClickCounter
    private var clickCounts: [MouseButton: Int] = [:]
    /// Where clicks are counted from: the cursor's location, plus the movement made while it
    /// stands still. During capture the cursor is held in place while it moves on another
    /// computer, so quick clicks on different things there stay single clicks.
    private var pointer = CGPoint.zero
    private var lastLocation: CGPoint?

    init(doubleClickInterval: TimeInterval) {
        clicks = ClickCounter(interval: doubleClickInterval, radius: Tunables.Input.clickRadius)
    }

    mutating func convert(_ captured: CapturedInput) -> ControlInput? {
        follow(captured)
        let position = ControlCoordinates.coord(captured.location)
        switch captured.event {
        case .pointerMoved(let dx, let dy):
            return .pointer(position: position, delta: ControlCoordinates.coord(CGPoint(x: dx, y: dy)))
        case .key(let key, let action):
            return .key(page: key.page, usage: key.usage, action: Self.code(action))
        case .button(let button, let isDown):
            guard button.number >= 0, button.number <= Self.maxButton else { return nil }
            let seconds = Double(captured.tapTime) / 1e9
            let count: Int
            if isDown {
                count = clicks.press(button, at: pointer, time: seconds)
                clickCounts[button] = count
            } else {
                count = clickCounts[button] ?? 1
            }
            return .button(
                UInt8(button.number), isDown: isDown, clickCount: UInt8(clamping: count), position: position)
        case .scroll(let delta):
            return .scroll(ControlScroll.stroke(delta), position: position)
        }
    }

    private mutating func follow(_ captured: CapturedInput) {
        if captured.location != lastLocation {
            pointer = captured.location
            lastLocation = captured.location
        } else if case .pointerMoved(let dx, let dy) = captured.event {
            pointer.x += dx
            pointer.y += dy
        }
    }

    static func code(_ action: KeyAction) -> UInt8 {
        switch action {
        case .down: 1
        case .up: 2
        case .repeated: 3
        }
    }

    static func action(_ code: UInt8) -> KeyAction {
        switch code {
        case 2: .up
        case 3: .repeated
        default: .down
        }
    }
}

/// Applies input another computer sends to this Mac.
@MainActor
enum Injection {
    static func apply(_ input: ControlInput, to injector: any InputInjecting) {
        switch input {
        case .pointer(let position, _):
            injector.moveCursor(to: ControlCoordinates.point(position))
        case .key(let page, let usage, let action):
            injector.key(HIDKey(page: page, usage: usage), InputForwarder.action(action))
        case .button(let button, let isDown, let clickCount, let position):
            injector.button(
                MouseButton(number: Int(button)), isDown: isDown, at: ControlCoordinates.point(position),
                clickCount: Int(clickCount))
        case .scroll(let stroke, let position):
            let point = ControlCoordinates.point(position)
            if point != injector.cursorLocation {
                injector.moveCursor(to: point)
            }
            injector.scroll(ControlScroll.delta(stroke))
        case .capsLock:
            break  // a state the input controller sets, not an event
        }
    }

    /// The keys of key events, for releasing them here.
    static func keys(_ inputs: [ControlInput]) -> [HIDKey] {
        inputs.compactMap { input in
            if case .key(let page, let usage, _) = input { HIDKey(page: page, usage: usage) } else { nil }
        }
    }
}
