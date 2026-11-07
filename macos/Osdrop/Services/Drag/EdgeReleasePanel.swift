import AppKit

/// Ends a drag of this Mac at the cursor: a drop target that takes the drag as a copy or refuses
/// it, so it slides back, or that takes its files to carry them on.
@MainActor
protocol EdgeReleasing: AnyObject {
    /// Shows the drop target around `point` (global display coordinates).
    func show(at point: CGPoint, accepting: Bool)
    /// Shows a drop target around `point` that takes the drag's files, receiving promised ones
    /// into the staging folder: `dropped` when the drag is dropped on it, `completion` once every
    /// file is readable.
    func capture(
        at point: CGPoint, dropped: @escaping () -> Void,
        completion: @escaping (Result<CapturedDrag, Error>) -> Void)
    func hide()
}

/// An `EdgeDropTarget` shown around the cursor only while a drag is released over it.
@MainActor
final class EdgeReleasePanel: EdgeReleasing {
    private let target: EdgeDropTarget
    private var accepting = false
    private var dropped: (() -> Void)?

    init(staging: DragStaging) {
        target = EdgeDropTarget(staging: staging)
        target.decide = { [weak self] in
            guard let self, accepting else { return .reject }
            dropped?()
            dropped = nil
            return .acceptAsCopy
        }
    }

    func show(at point: CGPoint, accepting: Bool) {
        self.accepting = accepting
        dropped = nil
        target.onCapture = nil
        target.show(over: Self.frame(around: point))
    }

    func capture(
        at point: CGPoint, dropped: @escaping () -> Void,
        completion: @escaping (Result<CapturedDrag, Error>) -> Void
    ) {
        accepting = true
        self.dropped = dropped
        target.onCapture = { [weak self] result in
            self?.target.onCapture = nil
            completion(result)
        }
        target.show(over: Self.frame(around: point))
    }

    func hide() {
        target.hide()
    }

    private static func frame(around point: CGPoint) -> NSRect {
        let size = Theme.Drag.releaseSize
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        let center = NSPoint(x: point.x, y: primaryHeight - point.y)
        return NSRect(x: center.x - size / 2, y: center.y - size / 2, width: size, height: size)
    }
}
