import CoreGraphics
import Foundation

/// Counts consecutive presses of a button the way macOS does for physical clicks: the same button,
/// within the double-click interval and close to the previous press. Injected clicks must carry
/// this count, or applications never see a double click.
struct ClickCounter {
    let interval: TimeInterval
    let radius: Double

    private var lastButton: MouseButton?
    private var lastLocation: CGPoint = .zero
    private var lastTime: TimeInterval = 0
    private var lastCount = 0

    init(interval: TimeInterval, radius: Double) {
        self.interval = interval
        self.radius = radius
    }

    /// The click count for a press at `time` seconds.
    mutating func press(_ button: MouseButton, at location: CGPoint, time: TimeInterval) -> Int {
        let distance = hypot(location.x - lastLocation.x, location.y - lastLocation.y)
        let continues = lastButton == button && time - lastTime <= interval && distance <= radius
        lastCount = continues ? lastCount + 1 : 1
        lastButton = button
        lastLocation = location
        lastTime = time
        return lastCount
    }

    mutating func reset() {
        lastButton = nil
        lastCount = 0
    }
}
