import CoreBridge
import SwiftUI

/// Transfers in the menu: each with its progress, and a way to cancel it while it runs.
struct TransfersSection: View {
    let transfers: TransfersController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Text("transfers.title")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
            ForEach(transfers.rows) { row in
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
                Text(row.isSending ? "transfers.toComputer \(peer)" : "transfers.fromComputer \(peer)")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                if row.state == .running {
                    ProgressView(value: row.fraction)
                        .accessibilityLabel(Text("transfers.progress"))
                    Text(
                        "transfers.bytes \(Self.size(row.bytesDone)) \(Self.size(row.bytesTotal))"
                    )
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                } else {
                    Text(Self.stateKey(row.state))
                        .font(Theme.Typography.caption)
                        .foregroundStyle(row.state == .failed ? Theme.Colors.warning : Theme.Colors.secondary)
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

    private static func size(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
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
