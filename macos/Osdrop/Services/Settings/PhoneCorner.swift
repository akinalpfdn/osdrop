import CoreGraphics
import Foundation

/// One of a screen's four corners.
enum ScreenCorner: String, Codable, CaseIterable, Sendable {
    case topLeft
    case topRight
    case bottomLeft
    case bottomRight
}

/// Where a phone's box sits on this Mac (DEVPLAN Phase 21): a corner of one screen, chosen per
/// phone in Settings › Devices (user decision 2026-11-24).
struct PhoneCorner: Codable, Equatable, Sendable {
    /// The display's UUID (`ScreenDescription.id`); nil, or a display that is not connected: the
    /// main screen.
    var screen: String?
    var corner: ScreenCorner = .bottomRight

    /// A rectangle of `size` in `corner` of `area`, `inset` from both of its sides. AppKit
    /// coordinates: y grows upwards.
    static func frame(of size: CGSize, at corner: ScreenCorner, in area: CGRect, inset: CGFloat) -> CGRect {
        let left = corner == .topLeft || corner == .bottomLeft
        let top = corner == .topLeft || corner == .topRight
        let x = left ? area.minX + inset : area.maxX - inset - size.width
        let y = top ? area.maxY - inset - size.height : area.minY + inset
        return CGRect(origin: CGPoint(x: x, y: y), size: size)
    }
}
