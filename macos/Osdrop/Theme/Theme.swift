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

    enum Artwork {
        /// A screen edge with a half arrow crossing it each way; a template image, so macOS tints it.
        static let menuBar = "MenuBarIcon"
    }

    enum Symbol {
        static let warning = "exclamationmark.triangle.fill"
        static let granted = "checkmark.circle.fill"
    }
}
