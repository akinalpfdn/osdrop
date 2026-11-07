import AppKit
import Observation
import SwiftUI

/// Shows the progress of copied files coming from another computer, in the screen's corner and in
/// front of every app, until they can be pasted (DEVPLAN Phase 15: a paste before then still gets
/// what the pasteboard had).
@MainActor
final class ClipboardFetchPresenter: NSObject, NSWindowDelegate {
    private let group: GroupController
    private var panel: NSPanel?

    init(group: GroupController) {
        self.group = group
        super.init()
        observe()
    }

    /// The window while it is shown, for the tests.
    var window: NSPanel? {
        panel?.isVisible == true ? panel : nil
    }

    private func observe() {
        withObservationTracking {
            _ = group.clipboard?.fetching
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                self?.observe()
            }
        }
        update()
    }

    private func update() {
        guard let clipboard = group.clipboard, clipboard.fetching != nil else {
            panel?.orderOut(nil)
            return
        }
        let panel = self.panel ?? makePanel(clipboard)
        self.panel = panel
        if !panel.isVisible {
            screen = Self.screenUnderCursor()
            place(panel)
        }
        panel.orderFrontRegardless()
    }

    /// The screen the window shows on: where the user is looking, the one under the cursor.
    private var screen: NSRect?

    private static func screenUnderCursor() -> NSRect? {
        let cursor = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { NSMouseInRect(cursor, $0.frame, false) } ?? NSScreen.main
        return screen?.visibleFrame
    }

    /// The window's top right corner in the screen's top right corner, inside it, whatever its size.
    static func origin(of size: NSSize, in screen: NSRect, margin: CGFloat) -> NSPoint {
        NSPoint(
            x: max(screen.minX, screen.maxX - size.width - margin),
            y: max(screen.minY, screen.maxY - size.height - margin))
    }

    private func place(_ panel: NSPanel) {
        guard let screen else { return }
        panel.setFrameOrigin(Self.origin(of: panel.frame.size, in: screen, margin: Theme.Spacing.large))
    }

    /// SwiftUI sizes the window once its content is laid out; AppKit keeps the bottom left corner,
    /// so the window is placed again to keep it in the corner.
    func windowDidResize(_ notification: Notification) {
        if let panel {
            place(panel)
        }
    }

    /// Like the pairing window: shown without activating Osdrop, kept in front while other apps
    /// are active.
    private func makePanel(_ clipboard: ClipboardController) -> NSPanel {
        let hosting = NSHostingController(rootView: ClipboardFetchView(group: group))
        hosting.sizingOptions = .preferredContentSize
        let panel = NSPanel(
            contentRect: .zero, styleMask: [.titled, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.contentViewController = hosting
        panel.title = String(localized: "app.name")
        panel.level = .floating
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.delegate = self
        return panel
    }
}

private struct ClipboardFetchView: View {
    let group: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            if let clipboard = group.clipboard, let fetch = clipboard.fetching {
                Text("clipboard.fetching \(fetch.name)")
                    .fixedSize(horizontal: false, vertical: true)
                ProgressView(value: fetch.fraction)
                    .accessibilityLabel(Text("transfers.progress"))
                Text("transfers.bytes \(Self.size(fetch.bytesDone)) \(Self.size(fetch.bytesTotal))")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                Text("clipboard.fetching.hint")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Spacer()
                    Button("clipboard.fetching.cancel") { clipboard.cancelFetch() }
                }
            }
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.fetchWindowWidth, alignment: .leading)
    }

    private static func size(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
    }
}
