import AppKit
import SwiftUI

/// The single source of visual values. Views use these tokens and never literal values.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 4
        static let medium: CGFloat = 8
        static let large: CGFloat = 12
        static let section: CGFloat = 16
    }

    enum Typography {
        static let title = Font.headline
        static let body = Font.body
        static let caption = Font.caption
        static let monospaced = Font.system(.caption, design: .monospaced)
    }

    enum Colors {
        static let secondary = Color.secondary
        static let warning = Color.orange
        static let positive = Color.green
    }

    enum Size {
        static let menuWidth: CGFloat = 280
        static let developerWindowWidth: CGFloat = 520
    }

    /// Drag windows are drawn with AppKit, so their values are AppKit types.
    enum Drag {
        /// Width of the strip along an edge that receives drags.
        static let edgeWidth: CGFloat = 6
        /// Nearly transparent: a window with no alpha at all is not a drop target.
        static let edgeFill = NSColor(white: 0, alpha: 0.01)
        /// The edge as the developer tools show it, so it can be found.
        static let highlightedEdgeFill = NSColor.systemGreen.withAlphaComponent(0.35)
        /// Size of the window a drag starts from.
        static let originSize: CGFloat = 48
        static let iconSize: CGFloat = 64
        /// How far each further item is shifted in a drag image.
        static let stackOffset: CGFloat = 6
    }

    enum Artwork {
        /// A screen edge with a half arrow crossing it each way; a template image, so macOS tints it.
        static let menuBar = "MenuBarIcon"
    }

    enum Symbol {
        static let warning = "exclamationmark.triangle.fill"
        static let granted = "checkmark.circle.fill"
    }
}
