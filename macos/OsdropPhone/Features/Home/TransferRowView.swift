import SwiftUI

/// One transfer: its progress while it runs, how it ended, and what can be done with a received
/// file (share it, or save a photo or video to Photos).
struct TransferRowView: View {
    let row: PhoneTransfers.Row
    /// The computer's name.
    let peer: String
    @Binding var notice: LocalizedStringKey?
    let cancel: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            Image(systemName: row.isSending ? Theme.Symbol.sending : Theme.Symbol.receiving)
                .foregroundStyle(Theme.Colors.secondary)
                .frame(width: Theme.Size.rowIcon)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(verbatim: row.name)
                    .lineLimit(2)
                Text(row.isSending ? "transfers.to \(peer)" : "transfers.from \(peer)")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
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
                Button(action: cancel) {
                    Label("transfers.cancel", systemImage: Theme.Symbol.cancel)
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
            } else if let file = row.file {
                if PhotoSaver.canSave(file) {
                    Button {
                        Task { await save(file) }
                    } label: {
                        Label("transfers.savePhoto", systemImage: Theme.Symbol.savePhoto)
                            .labelStyle(.iconOnly)
                    }
                    .buttonStyle(.borderless)
                }
                ShareLink(item: file) {
                    Label("transfers.share", systemImage: Theme.Symbol.share)
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
            }
        }
    }

    private func save(_ file: URL) async {
        do throws(PhotoSaver.Failure) {
            try await PhotoSaver.save(file)
            notice = "transfers.saved"
        } catch .notAllowed {
            notice = "transfers.photosNotAllowed"
        } catch {
            notice = "transfers.notSaved"
        }
    }

    private static func stateKey(_ state: PhoneTransfers.Row.State) -> LocalizedStringKey {
        switch state {
        case .running: "transfers.running"
        case .done: "transfers.done"
        case .failed: "transfers.failed"
        case .cancelled: "transfers.cancelled"
        }
    }
}
