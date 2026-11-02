import CoreGraphics
import Testing

@testable import Osdrop

@Suite("Replay plan")
struct ReplayPlanTests {
    private let bounds = CGRect(x: 0, y: 0, width: 100, height: 50)

    private func input(_ event: InputEvent, at milliseconds: UInt64) -> CapturedInput {
        CapturedInput(event: event, timestamp: milliseconds * 1_000_000)
    }

    @Test("Starts at the start point and adds up movements")
    func positions() {
        let plan = ReplayPlan(
            recording: [
                input(.pointerMoved(dx: 5, dy: 5), at: 0), input(.button(.left, isDown: true), at: 10),
                input(.pointerMoved(dx: 10, dy: -2), at: 20), input(.button(.left, isDown: false), at: 30),
            ], start: CGPoint(x: 10, y: 10), bounds: bounds, maximumGap: .seconds(1))
        #expect(
            plan.steps.map(\.action) == [
                .move(CGPoint(x: 10, y: 10)), .move(CGPoint(x: 15, y: 15)),
                .button(.left, isDown: true, at: CGPoint(x: 15, y: 15)), .move(CGPoint(x: 25, y: 13)),
                .button(.left, isDown: false, at: CGPoint(x: 25, y: 13)),
            ])
    }

    @Test("Keeps the cursor on the displays")
    func clamps() {
        let plan = ReplayPlan(
            recording: [input(.pointerMoved(dx: 500, dy: -500), at: 0)], start: CGPoint(x: -20, y: 10),
            bounds: bounds, maximumGap: .seconds(1))
        #expect(plan.steps.map(\.action) == [.move(CGPoint(x: 0, y: 10)), .move(CGPoint(x: 99, y: 0))])
    }

    @Test("Keeps the timing between events, shortening long pauses")
    func timing() {
        let plan = ReplayPlan(
            recording: [
                input(.key(.keyboard(0x04), .down), at: 100), input(.key(.keyboard(0x04), .up), at: 150),
                input(.scroll(ScrollDelta(x: 0, y: 1, unit: .line)), at: 5_000),
            ], start: .zero, bounds: bounds, maximumGap: .milliseconds(500))
        #expect(plan.steps.map(\.delay) == [.zero, .zero, .milliseconds(50), .milliseconds(500)])
    }

    @Test("Out-of-order timestamps replay without a pause")
    func outOfOrder() {
        let plan = ReplayPlan(
            recording: [input(.button(.left, isDown: true), at: 50), input(.button(.left, isDown: false), at: 40)],
            start: .zero, bounds: bounds, maximumGap: .seconds(1))
        #expect(plan.steps.last?.delay == .zero)
    }
}
