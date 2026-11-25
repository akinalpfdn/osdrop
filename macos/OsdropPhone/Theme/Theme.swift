import SwiftUI

/// Every visual value of the iPhone app.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 4
        static let medium: CGFloat = 8
        static let large: CGFloat = 16
    }

    enum Typography {
        static let code = Font.system(size: 44, weight: .semibold, design: .monospaced)
        static let caption = Font.caption
        static let body = Font.body
    }

    enum Colors {
        static let secondary = Color.secondary
        static let warning = Color.orange
    }

    enum Size {
        static let rowIcon: CGFloat = 28
    }

    enum Symbol {
        static let mac = "laptopcomputer"
        static let pc = "desktopcomputer"
        static let sendPhotos = "photo.on.rectangle"
        static let sendFiles = "doc"
        static let cancel = "xmark.circle.fill"
        static let savePhoto = "square.and.arrow.down"
        static let share = "square.and.arrow.up"
        static let receiving = "arrow.down.circle"
        static let sending = "arrow.up.circle"
        static let more = "ellipsis.circle"
    }
}
