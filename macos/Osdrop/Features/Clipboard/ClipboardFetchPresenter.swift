import AppKit
import Observation
import SwiftUI

/// Shows the progress of copied files coming from another computer, in the screen's corner and in
/// front of every app, until they can be pasted (DEVPLAN Phase 15: a paste before then still gets
/// what the pasteboard had).
@MainActor
final class ClipboardFetchPresenter {
    private let group: GroupController
    private var panel: NSPanel?

    init(group: GroupController) {
        self.group = group
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
        if !panel.isVisible, let screen = NSScreen.main?.visibleFrame {
            let size = panel.frame.size
            panel.setFrameOrigin(
                NSPoint(
                    x: screen.maxX - size.width - Theme.Spacing.large,
                    y: screen.maxY - size.height - Theme.Spacing.large))
        }
        panel.orderFrontRegardless()
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
