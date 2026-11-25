import AppKit

/// The screens a phone's box can sit on, and where on them.
@MainActor
enum CornerScreens {
    struct Choice: Identifiable, Hashable {
        /// The display's UUID, as `PhoneCorner.screen` keeps it.
        let id: String
        let name: String
        let isMain: Bool
    }

    /// Every connected screen, the main one (with the menu bar) first.
    static func choices() -> [Choice] {
        NSScreen.screens.enumerated().compactMap { index, screen in
            identifier(of: screen).map { Choice(id: $0, name: screen.localizedName, isMain: index == 0) }
        }
    }

    /// The chosen screen, or the main one when that one is not connected.
    struct Area {
        /// The whole screen: a drag reaches its very corner, below the Dock or beside it.
        let frame: CGRect
        /// The part windows may use: the box shows there, clear of the menu bar and the Dock.
        let visible: CGRect
    }

    static func area(for screenID: String?) -> Area? {
        let screens = NSScreen.screens
        let chosen = screenID.flatMap { id in screens.first { identifier(of: $0) == id } }
        return (chosen ?? screens.first).map { Area(frame: $0.frame, visible: $0.visibleFrame) }
    }

    private static func identifier(of screen: NSScreen) -> String? {
        let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber
        return number.map { CGScreenDescriber.identifier(of: CGDirectDisplayID($0.uint32Value)) }
    }
}
