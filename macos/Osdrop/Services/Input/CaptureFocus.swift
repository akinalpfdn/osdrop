import AppKit

/// Takes keyboard focus away from other apps while input is used elsewhere, so a focused password
/// field resigns and turns Secure Event Input off (DEVPLAN, macOS constraints).
@MainActor
protocol CaptureFocusing: AnyObject {
    /// True while Osdrop is the active app.
    var hasFocus: Bool { get }
    func acquire()
    func release()
}

/// Makes an invisible window of Osdrop's the key window, then gives focus back to the app that had
/// it. macOS 14 activates cooperatively and may refuse; `hasFocus` reports what happened.
@MainActor
final class InvisibleKeyWindowFocus: CaptureFocusing {
    private var window: NSWindow?
    private var previousApp: NSRunningApplication?

    var hasFocus: Bool {
        NSApp.isActive
    }

    func acquire() {
        let frontmost = NSWorkspace.shared.frontmostApplication
        if frontmost?.processIdentifier != ProcessInfo.processInfo.processIdentifier {
            previousApp = frontmost
        }
        let window = window ?? makeWindow()
        self.window = window
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
    }

    func release() {
        window?.orderOut(nil)
        if let previousApp, !previousApp.isTerminated {
            previousApp.activate(from: .current, options: [])
        }
        previousApp = nil
    }

    private func makeWindow() -> NSWindow {
        let window = KeyableWindow(
            contentRect: NSRect(origin: .zero, size: NSSize(width: 1, height: 1)), styleMask: .borderless,
            backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.isOpaque = false
        window.backgroundColor = .clear
        window.alphaValue = 0
        window.ignoresMouseEvents = true
        window.hasShadow = false
        window.collectionBehavior = [.canJoinAllSpaces, .transient, .ignoresCycle, .fullScreenAuxiliary]
        return window
    }
}

/// Borderless windows cannot become key unless they say so.
private final class KeyableWindow: NSWindow {
    override var canBecomeKey: Bool { true }
}
