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
    /// Called on the main actor whenever displays are added, removed, moved or change mode.
    var onChange: (() -> Void)? { get set }
}

@MainActor
final class CGScreenDescriber: ScreenDescribing {
    private static let maximumDisplays: UInt32 = 32

    var onChange: (() -> Void)?
    nonisolated(unsafe) private var observer: NSObjectProtocol?

    init() {
        observer = NotificationCenter.default.addObserver(
            forName: NSApplication.didChangeScreenParametersNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                Log.screens.info("Display configuration changed")
                self?.onChange?()
            }
        }
    }

    deinit {
        if let observer {
            NotificationCenter.default.removeObserver(observer)
        }
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

    private static func describe(_ display: CGDirectDisplayID) -> ScreenDescription {
        let frame = CGDisplayBounds(display)
        let mode = CGDisplayCopyDisplayMode(display)
        let uuid = CGDisplayCreateUUIDFromDisplayID(display)?.takeRetainedValue()
        let id = uuid.flatMap { CFUUIDCreateString(kCFAllocatorDefault, $0) as String? } ?? "display-\(display)"
        return ScreenDescription(
            id: id, displayID: display, frame: frame, pixelWidth: mode?.pixelWidth ?? Int(frame.width),
            pixelHeight: mode?.pixelHeight ?? Int(frame.height), isMain: CGDisplayIsMain(display) != 0)
    }
}
