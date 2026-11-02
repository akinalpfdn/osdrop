import SwiftUI

/// The single source of visual values. Views use these tokens and never literal values.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 4
        static let medium: CGFloat = 8
        static let large: CGFloat = 12
    }

    enum Typography {
        static let title = Font.headline
        static let caption = Font.caption
    }

    enum Size {
        static let menuWidth: CGFloat = 260
    }

    enum Symbol {
        static let menuBar = "cursorarrow.motionlines"
    }
}
