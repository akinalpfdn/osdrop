import AppKit
import ColorSync
import CoreGraphics

/// One display of this Mac, as the arrangement and the injector need it.
struct ScreenDescription: Equatable, Sendable, Identifiable {
    /// Stable across reboots and reconnections: the display's UUID.
    let id: String
    let displayID: CGDirectDisplayID
    /// In points, in global display coordinates (origin at the top left of the main display).
    let frame: CGRect
    let pixelWidth: Int
    let pixelHeight: Int
    let isMain: Bool

    var scale: Double {
        frame.width > 0 ? Double(pixelWidth) / frame.width : 1
    }
}

@MainActor
protocol ScreenDescribing: AnyObject {
    func screens() -> [ScreenDescription]
    /// Calls `handler` on the main actor whenever displays are added, removed, moved or change
    /// mode, until `stopObserving`. Several parts of the app follow the displays at once.
    func observe(_ handler: @escaping @MainActor () -> Void) -> ScreenObservation
    func stopObserving(_ observation: ScreenObservation)
}

/// One `observe` call's handle.
struct ScreenObservation: Hashable, Sendable {
    fileprivate(set) var id = UUID()
}

/// The handlers following the displays, for describers to keep.
@MainActor
struct ScreenObservers {
    private var handlers: [ScreenObservation: @MainActor () -> Void] = [:]

    mutating func add(_ handler: @escaping @MainActor () -> Void) -> ScreenObservation {
        let observation = ScreenObservation()
        handlers[observation] = handler
        return observation
    }

    mutating func remove(_ observation: ScreenObservation) {
        handlers[observation] = nil
    }

    func notify() {
        handlers.values.forEach { $0() }
    }
}

@MainActor
final class CGScreenDescriber: ScreenDescribing {
    private static let maximumDisplays: UInt32 = 32

    private var observers = ScreenObservers()
    nonisolated(unsafe) private var observer: NSObjectProtocol?

    init() {
        observer = NotificationCenter.default.addObserver(
            forName: NSApplication.didChangeScreenParametersNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                Log.screens.info("Display configuration changed")
                self?.observers.notify()
            }
        }
    }

    deinit {
        if let observer {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    func observe(_ handler: @escaping @MainActor () -> Void) -> ScreenObservation {
        observers.add(handler)
    }

    func stopObserving(_ observation: ScreenObservation) {
        observers.remove(observation)
    }

    func screens() -> [ScreenDescription] {
        var displays = [CGDirectDisplayID](repeating: 0, count: Int(Self.maximumDisplays))
        var count: UInt32 = 0
        guard CGGetActiveDisplayList(Self.maximumDisplays, &displays, &count) == .success else {
            Log.screens.error("Listing displays failed")
            return []
        }
        return displays.prefix(Int(count)).map(Self.describe)
    }

    /// The display's UUID, stable across reboots and reconnections.
    nonisolated static func identifier(of display: CGDirectDisplayID) -> String {
        let uuid = CGDisplayCreateUUIDFromDisplayID(display)?.takeRetainedValue()
        return uuid.flatMap { CFUUIDCreateString(kCFAllocatorDefault, $0) as String? } ?? "display-\(display)"
    }

    private static func describe(_ display: CGDirectDisplayID) -> ScreenDescription {
        let frame = CGDisplayBounds(display)
        let mode = CGDisplayCopyDisplayMode(display)
        return ScreenDescription(
            id: identifier(of: display), displayID: display, frame: frame,
            pixelWidth: mode?.pixelWidth ?? Int(frame.width),
            pixelHeight: mode?.pixelHeight ?? Int(frame.height), isMain: CGDisplayIsMain(display) != 0)
    }
}
