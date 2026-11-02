import CoreGraphics
import Darwin

/// Hides and shows this Mac's cursor while its input is used elsewhere.
protocol CursorHiding: Sendable {
    /// False when the cursor can only be hidden while Osdrop is the frontmost app.
    var hidesInBackground: Bool { get }
    func hide()
    func show()
}

/// Hides the cursor with CoreGraphics. A background app may hide it only after setting the private
/// `SetsCursorInBackground` connection property; Osdrop is a menu bar app and is rarely frontmost.
///
/// The private functions are looked up at run time, not linked, so a macOS release without them
/// degrades to foreground-only hiding instead of failing to launch (DEVPLAN, Risks). This type is
/// the only place that uses them.
final class BackgroundCursorHider: CursorHiding {
    private typealias DefaultConnection = @convention(c) () -> Int32
    private typealias SetConnectionProperty = @convention(c) (Int32, Int32, CFString, CFTypeRef) -> Int32

    let hidesInBackground: Bool
    private let isHidden = Locked(false)

    init() {
        hidesInBackground = Self.enableBackgroundHiding()
    }

    func hide() {
        isHidden.withLock { hidden in
            guard !hidden else { return }
            if CGDisplayHideCursor(CGMainDisplayID()) == .success {
                hidden = true
            } else {
                Log.input.error("Hiding the cursor failed")
            }
        }
    }

    func show() {
        isHidden.withLock { hidden in
            guard hidden else { return }
            if CGDisplayShowCursor(CGMainDisplayID()) != .success {
                Log.input.error("Showing the cursor failed")
            }
            hidden = false
        }
    }

    private static func enableBackgroundHiding() -> Bool {
        // RTLD_DEFAULT: search every image already loaded; SkyLight is loaded with AppKit.
        let everyImage = UnsafeMutableRawPointer(bitPattern: -2)
        guard let connectionSymbol = dlsym(everyImage, "_CGSDefaultConnection"),
            let propertySymbol = dlsym(everyImage, "CGSSetConnectionProperty")
        else {
            Log.input.error("SetsCursorInBackground is unavailable; the cursor hides only in the foreground")
            return false
        }
        let connection = unsafeBitCast(connectionSymbol, to: DefaultConnection.self)()
        let setProperty = unsafeBitCast(propertySymbol, to: SetConnectionProperty.self)
        let result = setProperty(connection, connection, "SetsCursorInBackground" as CFString, kCFBooleanTrue)
        if result != 0 {
            Log.input.error("SetsCursorInBackground failed with \(result, privacy: .public)")
        }
        return result == 0
    }
}
