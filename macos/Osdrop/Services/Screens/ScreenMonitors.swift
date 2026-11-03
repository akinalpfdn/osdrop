import CoreBridge
import CoreGraphics
import Foundation

extension ScreenDescription {
    /// This display as core takes it: points in 1/16, with the native rectangle the same as the
    /// frame, since a Mac's cursor moves in points.
    var monitor: ScreenMonitor {
        let rect = CoordRect(
            x: Self.coord(frame.minX), y: Self.coord(frame.minY), width: Self.coord(frame.width),
            height: Self.coord(frame.height))
        return ScreenMonitor(
            key: key, frame: rect, native: rect, pixelWidth: UInt16(clamping: pixelWidth),
            pixelHeight: UInt16(clamping: pixelHeight), isPrimary: isMain)
    }

    /// The display's UUID, or its display ID when it has none.
    private var key: [UInt8] {
        if let uuid = UUID(uuidString: id) {
            return withUnsafeBytes(of: uuid.uuid) { Array($0) }
        }
        var bytes = [UInt8](repeating: 0, count: 16)
        withUnsafeBytes(of: displayID.bigEndian) { bytes.replaceSubrange(0..<$0.count, with: $0) }
        return bytes
    }

    private static func coord(_ points: CGFloat) -> Int32 {
        Int32(clamping: Int((points * 16).rounded()))
    }
}
