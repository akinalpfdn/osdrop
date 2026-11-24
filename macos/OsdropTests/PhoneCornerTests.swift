import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osdrop

/// Where a phone's box sits (DEVPLAN Phase 21), and how the choice is kept.
@MainActor
@Suite("Phone corners")
struct PhoneCornerTests {
    private let area = CGRect(x: 100, y: 50, width: 1000, height: 800)
    private let size = CGSize(width: 300, height: 120)

    @Test("The box sits in each corner of the area, inset from both sides")
    func framesInEachCorner() {
        #expect(PhoneCorner.frame(of: size, at: .bottomRight, in: area, inset: 8) == CGRect(x: 792, y: 58, width: 300, height: 120))
        #expect(PhoneCorner.frame(of: size, at: .bottomLeft, in: area, inset: 8) == CGRect(x: 108, y: 58, width: 300, height: 120))
        #expect(PhoneCorner.frame(of: size, at: .topRight, in: area, inset: 8) == CGRect(x: 792, y: 722, width: 300, height: 120))
        #expect(PhoneCorner.frame(of: size, at: .topLeft, in: area, inset: 0) == CGRect(x: 100, y: 730, width: 300, height: 120))
    }

    @Test("A phone without a choice gets the main screen's bottom right corner; a choice is kept")
    func choiceIsKept() throws {
        let defaults = try #require(UserDefaults(suiteName: "osdrop-tests-\(UUID())"))
        let phone = try #require(DeviceID(bytes: [UInt8](repeating: 0x21, count: 16)))
        let settings = AppSettings(defaults: defaults)
        #expect(settings.corner(of: phone) == PhoneCorner(screen: nil, corner: .bottomRight))

        settings.phoneCorners[phone] = PhoneCorner(screen: "screen-uuid", corner: .topLeft)
        let reread = AppSettings(defaults: defaults)
        #expect(reread.corner(of: phone) == PhoneCorner(screen: "screen-uuid", corner: .topLeft))
    }
}
