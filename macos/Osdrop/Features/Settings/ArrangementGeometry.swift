import CoreBridge
import CoreGraphics

/// Every member's monitors in the shared space, for the arrangement editor (DEVPLAN Phase 16).
/// Units are core's: 1/16 of a logical point. A computer moves as a whole; its own monitors stay
/// where its system put them.
struct ArrangementGeometry: Equatable {
    struct Computer: Equatable, Identifiable {
        let deviceID: DeviceID
        let name: String
        let isThisMac: Bool
        let isConnected: Bool
        /// The computer's place in the shared space.
        var offset: CGPoint
        /// Its monitors' logical frames, relative to its own origin.
        let monitors: [CGRect]

        var id: DeviceID { deviceID }

        /// Its monitors where they are in the shared space.
        var placedMonitors: [CGRect] { monitors.map { $0.offsetBy(dx: offset.x, dy: offset.y) } }
        var bounds: CGRect { placedMonitors.reduce(CGRect.null) { $0.union($1) } }
    }

    var computers: [Computer]

    /// The computers placed in the arrangement and their monitors; detached ones and computers
    /// whose screens are not known yet are left out.
    init(layout: [LayoutComputer], members: [GroupMember]) {
        computers = layout.compactMap { computer in
            guard let placement = computer.placement, !placement.isDetached, !computer.monitors.isEmpty else {
                return nil
            }
            let member = members.first { $0.deviceID == computer.deviceID }
            return Computer(
                deviceID: computer.deviceID, name: member?.name ?? "", isThisMac: member?.isSelf ?? false,
                isConnected: member.map { $0.isSelf || $0.isConnected } ?? false,
                offset: CGPoint(x: Double(placement.x), y: Double(placement.y)),
                monitors: computer.monitors.map { monitor in
                    CGRect(
                        x: Double(monitor.frame.x), y: Double(monitor.frame.y), width: Double(monitor.frame.width),
                        height: Double(monitor.frame.height))
                })
        }
    }

    init(computers: [Computer]) {
        self.computers = computers
    }

    /// Everything, plus room around it to drag into.
    var bounds: CGRect {
        let all = computers.map(\.bounds).reduce(CGRect.null) { $0.union($1) }
        guard !all.isNull else { return .zero }
        let margin = max(all.width, all.height) * Theme.Arrangement.marginFraction
        return all.insetBy(dx: -margin, dy: -margin)
    }

    /// Where `device` lands when dropped with its origin at `drop`: touching the nearest edge of
    /// another computer, with at least half of the shorter of the two edges in common, so the
    /// cursor has room to cross.
    /// Alone in the arrangement, it stays where dropped.
    func snapped(_ device: DeviceID, droppedAt drop: CGPoint) -> CGPoint {
        guard let moving = computers.first(where: { $0.deviceID == device }) else { return drop }
        let size = moving.bounds.size
        let local = moving.bounds.origin.applying(.init(translationX: -moving.offset.x, y: -moving.offset.y))
        let dropped = CGRect(origin: CGPoint(x: drop.x + local.x, y: drop.y + local.y), size: size)
        var best: (point: CGPoint, distance: CGFloat)?
        for other in computers where other.deviceID != device {
            let target = other.bounds
            let sharedHeight = min(size.height, target.height) * Self.sharedEdgeFraction
            let sharedWidth = min(size.width, target.width) * Self.sharedEdgeFraction
            let overlapY = Self.clamp(
                dropped.minY, target.minY - size.height + sharedHeight, target.maxY - sharedHeight)
            let overlapX = Self.clamp(
                dropped.minX, target.minX - size.width + sharedWidth, target.maxX - sharedWidth)
            let candidates = [
                CGPoint(x: target.maxX, y: overlapY),  // to its right
                CGPoint(x: target.minX - size.width, y: overlapY),  // to its left
                CGPoint(x: overlapX, y: target.maxY),  // below it
                CGPoint(x: overlapX, y: target.minY - size.height),  // above it
            ]
            for candidate in candidates {
                let distance = hypot(candidate.x - dropped.minX, candidate.y - dropped.minY)
                if best == nil || distance < best?.distance ?? .infinity {
                    best = (candidate, distance)
                }
            }
        }
        guard let best else { return drop }
        return CGPoint(x: best.point.x - local.x, y: best.point.y - local.y)
    }

    /// Every placement in `layout`, detached computers included, with `device` moved to `offset`.
    static func placements(of layout: [LayoutComputer], moving device: DeviceID, to offset: CGPoint) -> [Placement] {
        layout.compactMap(\.placement).map { placement in
            guard placement.deviceID == device else { return placement }
            return Placement(
                deviceID: device, x: Int32(clamping: Int64(offset.x.rounded())),
                y: Int32(clamping: Int64(offset.y.rounded())), isDetached: false)
        }
    }

    /// The share of the shorter edge two touching computers keep in common.
    static let sharedEdgeFraction: CGFloat = 0.5

    private static func clamp(_ value: CGFloat, _ low: CGFloat, _ high: CGFloat) -> CGFloat {
        min(max(value, low), high)
    }
}
