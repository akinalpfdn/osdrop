import AppKit

/// Notices a drag in progress anywhere on this Mac: macOS writes the dragged items to the drag
/// pasteboard when a drag starts, so its change count moves while the left button is down
/// (DEVPLAN, macOS constraints). Checked on a timer while someone needs to know.
@MainActor
final class DragActivityMonitor {
    private(set) var isDragging = false
    var onChange: ((Bool) -> Void)?

    private let pasteboard = NSPasteboard(name: .drag)
    private var changeCountAtPress: Int?
    private var timer: Timer?

    func start() {
        guard timer == nil else { return }
        let timer = Timer(timeInterval: Tunables.Drag.monitorInterval.timeInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.check() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        changeCountAtPress = nil
        update(false)
    }

    private func check() {
        let isLeftDown = NSEvent.pressedMouseButtons & 1 != 0
        guard isLeftDown else {
            changeCountAtPress = nil
            update(false)
            return
        }
        guard let pressed = changeCountAtPress else {
            changeCountAtPress = pasteboard.changeCount
            return
        }
        update(pasteboard.changeCount != pressed)
    }

    private func update(_ dragging: Bool) {
        guard dragging != isDragging else { return }
        isDragging = dragging
        onChange?(dragging)
    }
}
