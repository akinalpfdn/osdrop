import AppKit
import CoreBridge
import SwiftUI

/// Transfers in the menu: the newest few, each with its progress and a way to cancel it while it
/// runs, and a way to the transfers window when there are more.
struct TransfersSection: View {
    let transfers: TransfersController
    let limit: Int
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            HStack {
                Text("transfers.title")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                Spacer()
                if transfers.rows.count > limit {
                    Button("transfers.showAll \(transfers.rows.count)") {
                        openWindow(id: TransfersWindowView.windowID)
                        NSApp.activate()
                    }
                    .buttonStyle(.link)
                    .font(Theme.Typography.caption)
                }
            }
            TransferRows(transfers: transfers, rows: transfers.rows.prefix(limit))
        }
    }
}

/// Every transfer the Mac remembers, newest first, in a window of its own.
struct TransfersWindowView: View {
    static let windowID = "transfers"
    let group: GroupController

    var body: some View {
        Group {
            if let transfers = group.transfers, !transfers.rows.isEmpty {
                ScrollView {
                    TransferRows(transfers: transfers, rows: transfers.rows[...])
                        .padding(Theme.Spacing.large)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            } else {
                Text("transfers.empty")
                    .foregroundStyle(Theme.Colors.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(minWidth: Theme.Size.menuWidth, minHeight: Theme.Size.menuWidth)
    }
}

private struct TransferRows: View {
    let transfers: TransfersController
    let rows: ArraySlice<TransfersController.Row>

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            ForEach(rows) { row in
                TransferRowView(row: row, peer: transfers.name(of: row)) {
                    transfers.cancel(row)
                }
            }
        }
    }
}

private struct TransferRowView: View {
    let row: TransfersController.Row
    let peer: String
    let cancel: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.small) {
            Image(systemName: row.isSending ? Theme.Symbol.sending : Theme.Symbol.receiving)
                .foregroundStyle(Theme.Colors.secondary)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(row.name)
                    .fixedSize(horizontal: false, vertical: true)
                if row.state == .running {
                    Text(direction)
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    ProgressView(value: row.fraction)
                        .accessibilityLabel(Text("transfers.progress"))
                    Text(
                        "transfers.bytes \(Self.size(row.bytesDone)) \(Self.size(row.bytesTotal))"
                    )
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                } else {
                    // One line once it ended, so a finished transfer takes little room.
                    Text("transfers.status \(direction) \(String(localized: Self.stateKey(row.state)))")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(row.state == .failed ? Theme.Colors.warning : Theme.Colors.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    if row.linksLeftOut > 0 {
                        Text(TransfersController.linksNote(row.linksLeftOut))
                            .font(Theme.Typography.caption)
                            .foregroundStyle(Theme.Colors.warning)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            Spacer(minLength: 0)
            if row.state == .running {
                Button(action: cancel) {
                    Label("transfers.cancel", systemImage: Theme.Symbol.cancel)
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
                .help(Text("transfers.cancel"))
            }
        }
        .accessibilityElement(children: .contain)
    }

    private var direction: String {
        row.isSending
            ? String(localized: "transfers.toComputer \(peer)") : String(localized: "transfers.fromComputer \(peer)")
    }

    private static func size(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
    }

    private static func stateKey(_ state: TransfersController.Row.State) -> String.LocalizationValue {
        switch state {
        case .running: "transfers.running"
        case .done: "transfers.done"
        case .failed: "transfers.failed"
        case .cancelled: "transfers.cancelled"
        }
    }
}
