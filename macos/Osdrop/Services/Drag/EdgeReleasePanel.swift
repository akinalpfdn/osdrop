import AppKit

/// Ends a drag of this Mac that waited at the edge: a drop target under the cursor that takes the
/// drag as a copy or refuses it, so it slides back.
@MainActor
protocol EdgeReleasing: AnyObject {
    /// Shows the drop target around `point` (global display coordinates).
    func show(at point: CGPoint, accepting: Bool)
    func hide()
}

/// An `EdgeDropTarget` shown around the cursor only while the drag is released.
@MainActor
final class EdgeReleasePanel: EdgeReleasing {
    private let target: EdgeDropTarget
    private var accepting = false

    init(staging: DragStaging) {
        target = EdgeDropTarget(staging: staging)
        target.decide = { [weak self] in
            self?.accepting == true ? .acceptAsCopy : .reject
        }
    }

    func show(at point: CGPoint, accepting: Bool) {
        self.accepting = accepting
        let size = Theme.Drag.releaseSize
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        let center = NSPoint(x: point.x, y: primaryHeight - point.y)
        target.show(over: NSRect(x: center.x - size / 2, y: center.y - size / 2, width: size, height: size))
    }

    func hide() {
        target.hide()
    }
}
