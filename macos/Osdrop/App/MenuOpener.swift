import AppKit

/// Opens the menu by itself: once at the first launch, so whoever installed Osdrop sees where it
/// lives, and whenever Osdrop is opened again while it runs (user decision 2026-11-23; the PC's
/// panel does the same).
@MainActor
final class MenuOpener {
    private let settings: AppSettings
    private let click: () -> Void

    init(settings: AppSettings, click: @escaping () -> Void = { MenuBarIcon.click() }) {
        self.settings = settings
        self.click = click
    }

    /// The first launch on this Mac opens the menu; later launches leave it closed.
    func openAtFirstLaunch() {
        guard !settings.hasShownMenu else { return }
        settings.hasShownMenu = true
        click()
    }

    func open() {
        click()
    }
}

/// The menu bar icon SwiftUI's `MenuBarExtra` makes. SwiftUI has no call that opens the menu, so the
/// icon's button is clicked the way the user would; should a macOS release build the icon another
/// way, the menu just stays closed.
@MainActor
enum MenuBarIcon {
    static func click(attempts: Int = Tunables.Menu.openAttempts) {
        guard let button = button() else {
            guard attempts > 1 else {
                Log.app.error("The menu bar icon was not found; the menu did not open")
                return
            }
            Task {
                try? await Task.sleep(for: Tunables.Menu.openRetry)
                click(attempts: attempts - 1)
            }
            return
        }
        let shown = visibleWindows()
        button.performClick(nil)
        // The log tells whether the click still opens the menu on this macOS release.
        Task {
            try? await Task.sleep(for: Tunables.Menu.openCheck)
            if visibleWindows().isSubset(of: shown) {
                Log.app.error("The menu bar icon was clicked, but no menu opened")
            } else {
                Log.app.info("The menu opened by itself")
            }
        }
    }

    private static func visibleWindows() -> Set<ObjectIdentifier> {
        Set(NSApp.windows.filter(\.isVisible).map(ObjectIdentifier.init))
    }

    private static func button() -> NSStatusBarButton? {
        NSApp.windows.lazy.compactMap { statusButton(in: $0.contentView) }.first
    }

    private static func statusButton(in view: NSView?) -> NSStatusBarButton? {
        guard let view else { return nil }
        if let button = view as? NSStatusBarButton { return button }
        return view.subviews.lazy.compactMap { statusButton(in: $0) }.first
    }
}
