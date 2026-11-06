import AppKit
import SwiftUI

/// A menu bar app is never the active app, so the Settings window SwiftUI opens stays behind the
/// frontmost app's windows. The Settings view reports its window here; it is ordered in front of
/// every app when it first appears and whenever the Settings button is clicked again.
@MainActor
enum SettingsWindow {
    private static weak var window: NSWindow?

    /// After `openSettings`: fronts the window if it exists; a new one fronts itself on appearing.
    static func bringToFront() {
        front()
    }

    fileprivate static func attached(_ window: NSWindow) {
        self.window = window
        front()
    }

    private static func front() {
        guard let window else { return }
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
    }
}

/// Finds the NSWindow hosting the Settings view and hands it to `SettingsWindow`.
struct SettingsWindowReader: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = ReportingView()
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}

    private final class ReportingView: NSView {
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let window { SettingsWindow.attached(window) }
        }
    }
}
