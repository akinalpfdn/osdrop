import AppKit

/// Opens the menu by itself: once at the first launch, so whoever installed Osdrop sees where it
/// lives, and whenever Osdrop is opened again while it runs (user decision 2026-11-23; the PC's
/// panel does the same).
@MainActor
final class MenuOpener {
    private let settings: AppSettings
    private let show: () -> Void

    init(settings: AppSettings, show: @escaping () -> Void = { MenuBarIcon.openMenu() }) {
        self.settings = settings
        self.show = show
    }

    /// The first launch on this Mac opens the menu; later launches leave it closed.
    func openAtFirstLaunch() {
        guard !settings.hasShownMenu else { return }
        settings.hasShownMenu = true
        show()
    }

    func open() {
        show()
    }
}

/// The status item SwiftUI's `MenuBarExtra` makes, and how its menu opens without a click; SwiftUI
/// has no call for this. Up to macOS 26 a click on the item's button opens it, so the button is
/// clicked. From macOS 27 the button does nothing: a click begins an AppKit "expanded interface
/// session", which only AppKit's private `_beginExpandedInterfaceSession:` begins, as the
/// MenuBarExtraAccess library does too (user decision 2026-11-23). A private member is used only
/// while AppKit has it with the signature it has in macOS 27.2; otherwise the menu stays closed and
/// the log says so.
@MainActor
enum MenuBarIcon {
    /// Opens the menu unless it is open, once SwiftUI has made the status item.
    static func openMenu(attempts: Int = Tunables.Menu.openAttempts) {
        guard let item = statusItem() else {
            guard attempts > 1 else {
                Log.app.error("The menu bar icon was not found; the menu did not open")
                return
            }
            Task {
                try? await Task.sleep(for: Tunables.Menu.openRetry)
                openMenu(attempts: attempts - 1)
            }
            return
        }
        let shown = visibleWindows()
        guard present(item) else { return }
        // The log tells whether this still opens the menu on this macOS release.
        Task {
            try? await Task.sleep(for: Tunables.Menu.openCheck)
            if visibleWindows().isSubset(of: shown) {
                Log.app.error("Opening the menu by itself showed no window")
            } else {
                Log.app.info("The menu opened by itself")
            }
        }
    }

    /// The status item SwiftUI made, from the menu bar window that holds it (AppKit's private
    /// `statusItem`); nil until it exists.
    static func statusItem() -> NSStatusItem? {
        for window in NSApp.windows where window.responds(to: Private.statusItem) {
            // A copy shows the item on each further display that has its own Space; the original
            // is the one that opens the menu.
            if let item = window.value(forKey: Private.statusItemKey) as? NSStatusItem,
                !item.className.contains("Replicant")
            {
                return item
            }
        }
        return nil
    }

    /// Whether instances of `type` have `selector` with exactly this signature (type encoding).
    static func has(_ selector: Selector, signature: String, in type: AnyClass) -> Bool {
        guard let method = class_getInstanceMethod(type, selector), let encoding = method_getTypeEncoding(method)
        else { return false }
        return String(cString: encoding) == signature
    }

    /// Opens the menu; false when it is open already or cannot be opened.
    private static func present(_ item: NSStatusItem) -> Bool {
        if object(Private.sessionDelegate, of: item) != nil {  // macOS 27 and later
            guard object(Private.session, of: item) == nil else { return false }
            guard has(Private.beginSession, signature: Private.beginSessionSignature, in: type(of: item)),
                let implementation = item.method(for: Private.beginSession)
            else {
                Log.app.error("This macOS release has no known way to open the menu by itself")
                return false
            }
            typealias BeginSession = @convention(c) (NSStatusItem, Selector, TimeInterval) -> Void
            // The time of the event that began the session: AppKit drops a begin older than the end
            // of the last session, and without an event the latest possible time always passes.
            unsafeBitCast(implementation, to: BeginSession.self)(item, Private.beginSession, .greatestFiniteMagnitude)
            return true
        }
        guard let button = item.button, button.state == .off else { return false }
        button.performClick(nil)
        return true
    }

    /// The object a private getter returns, when AppKit has the getter with the expected signature.
    private static func object(_ selector: Selector, of item: NSStatusItem) -> AnyObject? {
        guard has(selector, signature: Private.getterSignature, in: type(of: item)) else { return nil }
        return item.perform(selector)?.takeUnretainedValue()
    }

    private static func visibleWindows() -> Set<ObjectIdentifier> {
        Set(NSApp.windows.filter(\.isVisible).map(ObjectIdentifier.init))
    }

    @MainActor
    private enum Private {
        static let statusItemKey = "statusItem"
        static let statusItem = NSSelectorFromString(statusItemKey)
        static let sessionDelegate = NSSelectorFromString("expandedInterfaceDelegate")
        static let session = NSSelectorFromString("expandedInterfaceSession")
        static let beginSession = NSSelectorFromString("_beginExpandedInterfaceSession:")
        /// Returns nothing; takes the receiver, the selector and a double.
        static let beginSessionSignature = "v24@0:8d16"
        /// Returns an object; takes the receiver and the selector.
        static let getterSignature = "@16@0:8"
    }
}
