import AppKit
import CoreBridge
import Observation
import SwiftUI

/// One phone's box in its corner: a drop target that a drag reaches in the corner, and a small
/// panel listing what went to the phone and what came from it since the box opened.
@MainActor
@Observable
final class CornerBox {
    enum Mode: Equatable {
        /// Nothing shows.
        case hidden
        /// A drag is on this Mac: the corner takes it, the box does not show yet.
        case waiting
        /// The drag is over the corner: the box shows where it would be dropped.
        case hovering
        /// The box shows its transfers.
        case open
    }

    private(set) var mode: Mode = .hidden
    let transfers: TransfersController
    @ObservationIgnored var onDrop: (([URL]) -> Void)?
    /// A file is being dragged out of the box.
    @ObservationIgnored var onDragOut: (() -> Void)?

    @ObservationIgnored private let phone: DeviceID
    @ObservationIgnored private let nameOf: @MainActor () -> String
    @ObservationIgnored private let place: @MainActor () -> PhoneCorner
    @ObservationIgnored private let target: EdgeDropTarget
    @ObservationIgnored private let panel: NSPanel
    /// Transfers listed before the box opened: not its own.
    @ObservationIgnored private var hidden: Set<TransfersController.Row.Key> = []
    @ObservationIgnored private var dragging = false
    @ObservationIgnored private var lingering: Task<Void, Never>?
    @ObservationIgnored private var contentHeight: CGFloat = 0

    init(
        phone: DeviceID, name: @escaping @MainActor () -> String, transfers: TransfersController,
        staging: DragStaging, place: @escaping @MainActor () -> PhoneCorner
    ) {
        self.phone = phone
        nameOf = name
        self.transfers = transfers
        self.place = place
        target = EdgeDropTarget(staging: staging)
        panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: Theme.Corner.boxWidth, height: Theme.Corner.hotZone),
            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        // Below the drop target (status bar level), which takes drags over the box.
        panel.level = .floating
        panel.hidesOnDeactivate = false
        panel.becomesKeyOnlyIfNeeded = true
        panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .ignoresCycle, .fullScreenAuxiliary]
        let content = NSHostingView(rootView: CornerBoxView(box: self))
        content.sizingOptions = []  // the box sizes the panel from the view's height
        panel.contentView = content
        target.onEnter = { [weak self] _, _ in self?.entered() }
        target.onExit = { [weak self] in self?.exited() }
        target.decide = { .acceptAsCopy }
        target.onCapture = { [weak self] result in self?.captured(result) }
    }

    var name: String { nameOf() }

    /// The transfers with this phone since the box opened, newest first.
    var rows: [TransfersController.Row] {
        transfers.rows.filter { $0.peer == phone && !hidden.contains($0.id) }
    }

    // MARK: - Drags on this Mac

    func dragStarted() {
        dragging = true
        if mode == .hidden {
            mode = .waiting
        }
        layout()
    }

    func dragEnded() {
        dragging = false
        target.hide()
        if mode == .waiting || mode == .hovering {
            mode = .hidden
        }
        layout()
    }

    private func entered() {
        guard mode == .waiting else { return }
        mode = .hovering
        layout()
    }

    private func exited() {
        guard mode == .hovering else { return }
        mode = .waiting
        layout()
    }

    private func captured(_ result: Result<CapturedDrag, Error>) {
        switch result {
        case .success(let drag):
            let urls = drag.items.compactMap { drag.sources[$0.id] }
            Log.drag.info("\(urls.count, privacy: .public) items dropped in a phone's box")
            open()
            onDrop?(urls)
        case .failure(let error):
            Log.drag.error("Taking files dropped in a phone's box failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    // MARK: - Showing

    /// Shows the box with the transfers that start from now on.
    func open() {
        if mode != .open {
            hidden = Set(transfers.rows.filter { $0.peer == phone }.map(\.id))
            mode = .open
        }
        lingering?.cancel()
        layout()
        watchRows()
    }

    func close() {
        lingering?.cancel()
        mode = dragging ? .waiting : .hidden
        hidden = []
        layout()
    }

    func showInFinder(_ row: TransfersController.Row) {
        NSWorkspace.shared.activateFileViewerSelecting(row.files)
    }

    func cancel(_ row: TransfersController.Row) {
        transfers.cancel(row)
    }

    func dragOutStarted() {
        onDragOut?()
    }

    /// The view's height changed.
    func resized(to height: CGFloat) {
        guard abs(height - contentHeight) > 0.5 else { return }
        contentHeight = height
        layout()
    }

    private func layout() {
        let corner = place()
        guard let area = CornerScreens.area(for: corner.screen) else { return }
        let box = PhoneCorner.frame(
            of: CGSize(width: Theme.Corner.boxWidth, height: max(contentHeight, Theme.Corner.hotZone)),
            at: corner.corner, in: area, inset: Theme.Corner.inset)
        switch mode {
        case .hidden:
            panel.orderOut(nil)
        case .waiting:
            panel.orderOut(nil)
        case .hovering, .open:
            panel.setFrame(box, display: true)
            panel.orderFrontRegardless()
        }
        guard dragging, mode != .hidden else {
            target.hide()
            return
        }
        // The corner itself while the box is closed; the whole box once it shows.
        let zone = CGSize(width: Theme.Corner.hotZone, height: Theme.Corner.hotZone)
        target.show(over: mode == .waiting ? PhoneCorner.frame(of: zone, at: corner.corner, in: area, inset: 0) : box)
    }

    /// A box that only sent files closes a moment after the last one ended.
    private func watchRows() {
        let rows = withObservationTracking { self.rows } onChange: { [weak self] in
            Task { @MainActor in
                guard let self, self.mode == .open else { return }
                self.watchRows()
            }
        }
        lingering?.cancel()
        guard !rows.isEmpty, rows.allSatisfy({ $0.isSending && $0.state != .running }) else { return }
        lingering = Task { [weak self] in
            try? await Task.sleep(for: Tunables.Corner.sentLinger)
            guard let self, !Task.isCancelled, mode == .open else { return }
            close()
        }
    }
}

private struct CornerBoxView: View {
    let box: CornerBox

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            HStack(spacing: Theme.Spacing.small) {
                Image(systemName: Theme.Symbol.phone)
                    .accessibilityHidden(true)
                Text(verbatim: box.name)
                    .font(Theme.Typography.title)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
                if box.mode == .open {
                    Button {
                        box.close()
                    } label: {
                        Label("corner.close", systemImage: Theme.Symbol.close)
                            .labelStyle(.iconOnly)
                    }
                    .buttonStyle(.borderless)
                    .help(Text("corner.close"))
                }
            }
            if box.mode == .hovering || box.rows.isEmpty {
                Text("corner.dropHere \(box.name)")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                        ForEach(box.rows) { row in
                            CornerRowView(box: box, row: row)
                        }
                    }
                }
                .frame(maxHeight: Theme.Corner.maxListHeight)
                .fixedSize(horizontal: false, vertical: true)
                if box.rows.contains(where: { !$0.files.isEmpty }) {
                    Text("corner.dragOut")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Corner.boxWidth, alignment: .leading)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: Theme.Corner.cornerRadius))
        .onGeometryChange(for: CGFloat.self) { $0.size.height } action: { box.resized(to: $0) }
    }
}

private struct CornerRowView: View {
    let box: CornerBox
    let row: TransfersController.Row

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            icon
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(row.name)
                    .fixedSize(horizontal: false, vertical: true)
                if row.state == .running {
                    ProgressView(value: row.fraction)
                        .accessibilityLabel(Text("transfers.progress"))
                } else {
                    Text(Self.stateKey(row.state))
                        .font(Theme.Typography.caption)
                        .foregroundStyle(row.state == .failed ? Theme.Colors.warning : Theme.Colors.secondary)
                }
            }
            Spacer(minLength: 0)
            if row.state == .running {
                Button {
                    box.cancel(row)
                } label: {
                    Label("transfers.cancel", systemImage: Theme.Symbol.cancel)
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
                .help(Text("transfers.cancel"))
            } else if !row.files.isEmpty {
                Button {
                    box.showInFinder(row)
                } label: {
                    Label("corner.showInFinder", systemImage: Theme.Symbol.showInFinder)
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
                .help(Text("corner.showInFinder"))
            }
        }
        .contentShape(Rectangle())
        .modifier(FileDragSource(url: row.files.first, started: box.dragOutStarted))
        .accessibilityElement(children: .contain)
    }

    @ViewBuilder private var icon: some View {
        if let url = row.files.first {
            Image(nsImage: NSWorkspace.shared.icon(forFile: url.path))
                .resizable()
                .frame(width: Theme.Corner.rowIcon, height: Theme.Corner.rowIcon)
                .accessibilityHidden(true)
        } else {
            Image(systemName: row.isSending ? Theme.Symbol.sending : Theme.Symbol.receiving)
                .font(Theme.Typography.symbol)
                .foregroundStyle(Theme.Colors.secondary)
                .frame(width: Theme.Corner.rowIcon, height: Theme.Corner.rowIcon)
                .accessibilityHidden(true)
        }
    }

    private static func stateKey(_ state: TransfersController.Row.State) -> LocalizedStringKey {
        switch state {
        case .running: "transfers.running"
        case .done: "transfers.done"
        case .failed: "transfers.failed"
        case .cancelled: "transfers.cancelled"
        }
    }
}

/// A received file dragged out of the box, into Finder or any app, as a copy.
private struct FileDragSource: ViewModifier {
    let url: URL?
    let started: () -> Void

    func body(content: Content) -> some View {
        if let url {
            content.onDrag {
                started()
                return NSItemProvider(contentsOf: url) ?? NSItemProvider()
            }
        } else {
            content
        }
    }
}
