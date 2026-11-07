import CoreBridge
import CoreGraphics
import Testing

@testable import Osdrop

/// The arrangement editor's geometry: a dropped computer snaps beside the nearest other one and
/// keeps an edge in common, and detached computers keep their place.
@Suite("Arrangement geometry")
struct ArrangementGeometryTests {
    /// Points in core's units (1/16 point).
    private static func units(_ points: Double) -> Double { points * 16 }

    private static func computer(
        _ byte: UInt8, at x: Double, _ y: Double, width: Double = 1000, height: Double = 800
    )
        -> ArrangementGeometry.Computer
    {
        ArrangementGeometry.Computer(
            deviceID: id(byte), name: "Computer \(byte)", isThisMac: byte == 1, isConnected: true,
            offset: CGPoint(x: units(x), y: units(y)),
            monitors: [CGRect(x: 0, y: 0, width: units(width), height: units(height))])
    }

    private static func id(_ byte: UInt8) -> DeviceID {
        guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
            preconditionFailure("16 bytes always make a device ID")
        }
        return id
    }

    @Test("A computer dropped near another one's right edge snaps against it, at the height it was dropped")
    func snapsToTheNearestEdge() {
        let geometry = ArrangementGeometry(computers: [Self.computer(1, at: 0, 0), Self.computer(2, at: 1500, 0)])
        let snapped = geometry.snapped(Self.id(2), droppedAt: CGPoint(x: Self.units(1100), y: Self.units(200)))
        #expect(snapped == CGPoint(x: Self.units(1000), y: Self.units(200)))
        let left = geometry.snapped(Self.id(2), droppedAt: CGPoint(x: Self.units(-1100), y: Self.units(-50)))
        #expect(left == CGPoint(x: Self.units(-1000), y: Self.units(-50)))
    }

    @Test("A computer dropped far away keeps at least half an edge in common, so the cursor can cross")
    func keepsAnEdgeInCommon() {
        let geometry = ArrangementGeometry(computers: [Self.computer(1, at: 0, 0), Self.computer(2, at: 1500, 0)])
        let snapped = geometry.snapped(Self.id(2), droppedAt: CGPoint(x: Self.units(3000), y: Self.units(5000)))
        let bounds = CGRect(origin: snapped, size: CGSize(width: Self.units(1000), height: Self.units(800)))
        let other = CGRect(x: 0, y: 0, width: Self.units(1000), height: Self.units(800))
        let sharedHeight = min(bounds.maxY, other.maxY) - max(bounds.minY, other.minY)
        let sharedWidth = min(bounds.maxX, other.maxX) - max(bounds.minX, other.minX)
        let sharesVertical = bounds.minX == other.maxX && sharedHeight >= Self.units(400)
        let sharesHorizontal = bounds.minY == other.maxY && sharedWidth >= Self.units(500)
        #expect(sharesVertical || sharesHorizontal)
    }

    @Test("Alone in the arrangement, a computer stays where it is dropped")
    func aloneStays() {
        let geometry = ArrangementGeometry(computers: [Self.computer(1, at: 0, 0)])
        #expect(geometry.snapped(Self.id(1), droppedAt: CGPoint(x: 40, y: 50)) == CGPoint(x: 40, y: 50))
    }

    @Test("Placements keep every computer, detached ones included, and move only the dropped one")
    func placementsKeepEveryone() {
        let layout = [
            LayoutComputer(
                deviceID: Self.id(1), placement: Placement(deviceID: Self.id(1), x: 0, y: 0, isDetached: false),
                monitors: []),
            LayoutComputer(
                deviceID: Self.id(2), placement: Placement(deviceID: Self.id(2), x: 16_000, y: 0, isDetached: false),
                monitors: []),
            LayoutComputer(
                deviceID: Self.id(3), placement: Placement(deviceID: Self.id(3), x: 99, y: 99, isDetached: true),
                monitors: []),
            LayoutComputer(deviceID: Self.id(4), placement: nil, monitors: []),
        ]
        let placements = ArrangementGeometry.placements(of: layout, moving: Self.id(2), to: CGPoint(x: -16_000, y: 3))
        #expect(
            placements == [
                Placement(deviceID: Self.id(1), x: 0, y: 0, isDetached: false),
                Placement(deviceID: Self.id(2), x: -16_000, y: 3, isDetached: false),
                Placement(deviceID: Self.id(3), x: 99, y: 99, isDetached: true),
            ])
    }

    @Test("Computers without screens or detached are not drawn")
    func leavesOutWhatCannotBeDrawn() {
        let monitor = ScreenMonitor(
            key: [UInt8](repeating: 1, count: 16), frame: CoordRect(x: 0, y: 0, width: 16_000, height: 12_800),
            native: CoordRect(x: 0, y: 0, width: 16_000, height: 12_800), pixelWidth: 2000, pixelHeight: 1600,
            isPrimary: true)
        let geometry = ArrangementGeometry(
            layout: [
                LayoutComputer(
                    deviceID: Self.id(1), placement: Placement(deviceID: Self.id(1), x: 0, y: 0, isDetached: false),
                    monitors: [monitor]),
                LayoutComputer(
                    deviceID: Self.id(2), placement: Placement(deviceID: Self.id(2), x: 0, y: 0, isDetached: true),
                    monitors: [monitor]),
                LayoutComputer(
                    deviceID: Self.id(3), placement: Placement(deviceID: Self.id(3), x: 0, y: 0, isDetached: false),
                    monitors: []),
            ], members: [])
        #expect(geometry.computers.map(\.deviceID) == [Self.id(1)])
    }
}
