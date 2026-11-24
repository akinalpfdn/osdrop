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

    /// The part of the chosen screen windows may use; the main screen's when that one is not
    /// connected.
    static func area(for screenID: String?) -> CGRect? {
        let screens = NSScreen.screens
        let chosen = screenID.flatMap { id in screens.first { identifier(of: $0) == id } }
        return (chosen ?? screens.first)?.visibleFrame
    }

    private static func identifier(of screen: NSScreen) -> String? {
        let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber
        return number.map { CGScreenDescriber.identifier(of: CGDirectDisplayID($0.uint32Value)) }
    }
}
