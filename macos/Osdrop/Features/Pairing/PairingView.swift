import SwiftUI

/// The temporary pairing window (final UI in Phase 16): the code both computers show and the
/// user's answer.
struct PairingView: View {
    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.large) {
            switch controller.pairing {
            case .dialing(let name):
                Text("pairing.dialing \(name)")
                    .fixedSize(horizontal: false, vertical: true)
                ProgressView()
                    .controlSize(.small)
                closeButton
            case .confirm(_, let code, let name):
                Text("pairing.confirm \(name)")
                    .font(Theme.Typography.title)
                    .fixedSize(horizontal: false, vertical: true)
                codeText(code)
                Text("pairing.confirm.hint")
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Spacer()
                    Button("pairing.reject") { controller.answerPairing(accepted: false) }
                        .keyboardShortcut(.cancelAction)
                    Button("pairing.accept") { controller.answerPairing(accepted: true) }
                        .keyboardShortcut(.defaultAction)
                }
            case .waiting(let code, let name):
                codeText(code)
                Text("pairing.waiting \(name)")
                    .fixedSize(horizontal: false, vertical: true)
                ProgressView()
                    .controlSize(.small)
                closeButton
            case .failed(let name, let reason):
                Label("pairing.failed \(name)", systemImage: Theme.Symbol.failed)
                    .font(Theme.Typography.title)
                    .fixedSize(horizontal: false, vertical: true)
                Text(reason.messageKey)
                    .fixedSize(horizontal: false, vertical: true)
                closeButton
            case nil:
                EmptyView()
            }
        }
        .padding(Theme.Spacing.section)
        .frame(width: Theme.Size.pairingWindowWidth, alignment: .leading)
    }

    private var closeButton: some View {
        HStack {
            Spacer()
            Button("pairing.close") { controller.dismissPairing() }
                .keyboardShortcut(.cancelAction)
        }
    }

    private func codeText(_ code: UInt32) -> some View {
        Text(PairingCode.display(code))
            .font(Theme.Typography.pairingCode)
            .frame(maxWidth: .infinity)
            .accessibilityLabel(Text(PairingCode.spoken(code)))
    }
}

enum PairingCode {
    /// "042 917": six digits with leading zeros, in two groups.
    static func display(_ code: UInt32) -> String {
        let digits = String(format: "%06u", code % 1_000_000)
        return digits.prefix(3) + " " + digits.suffix(3)
    }

    /// One digit at a time, for VoiceOver.
    static func spoken(_ code: UInt32) -> String {
        String(format: "%06u", code % 1_000_000).map(String.init).joined(separator: " ")
    }
}

extension PairingFailure {
    var messageKey: LocalizedStringKey {
        switch self {
        case .rejected: "pairing.failed.rejected"
        case .busy: "pairing.failed.busy"
        case .groupFull: "pairing.failed.groupFull"
        case .inAnotherGroup: "pairing.failed.inAnotherGroup"
        case .timedOut: "pairing.failed.timedOut"
        case .unreachable: "pairing.failed.unreachable"
        }
    }
}
