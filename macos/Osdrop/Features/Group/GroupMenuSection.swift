import CoreBridge
import SwiftUI

/// The group in the menu (temporary, final UI in Phase 16): members, nearby computers to pair
/// with, and what keeps Osdrop from finding them.
struct GroupMenuSection: View {
    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            if let failure = controller.startFailure {
                Label(failure.messageKey, systemImage: Theme.Symbol.warning)
                    .foregroundStyle(Theme.Colors.warning)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if controller.localNetwork == .denied {
                LocalNetworkBanner(controller: controller)
            }
            if controller.wasRemoved {
                RemovedBanner(controller: controller)
            }
            if let clipboard = controller.clipboard, let notice = clipboard.notice {
                ClipboardBanner(clipboard: clipboard, notice: notice)
            }
            if let input = controller.input {
                ControlStatus(input: input, controller: controller)
            }
            if controller.isRunning {
                members
                nearby
            }
        }
        .font(Theme.Typography.body)
    }

    @ViewBuilder
    private var members: some View {
        Text("group.members")
            .font(Theme.Typography.title)
        if controller.members.isEmpty {
            Text("group.empty")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
                .fixedSize(horizontal: false, vertical: true)
        } else {
            ForEach(controller.members) { member in
                MemberRow(member: member, isResponsive: !controller.unresponsive.contains(member.deviceID)) {
                    controller.revoke(member)
                }
            }
            Button("group.leave") { controller.leaveGroup() }
        }
    }

    @ViewBuilder
    private var nearby: some View {
        Text("group.nearby")
            .font(Theme.Typography.title)
        if controller.nearby.isEmpty {
            Text("group.nearby.none")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
                .fixedSize(horizontal: false, vertical: true)
        } else {
            ForEach(controller.nearby) { computer in
                NearbyRow(computer: computer) { controller.pair(with: computer) }
            }
        }
    }
}

private struct MemberRow: View {
    let member: GroupMember
    let isResponsive: Bool
    let remove: () -> Void

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: Theme.Spacing.medium) {
            Circle()
                .fill(statusColor)
                .frame(width: Theme.Size.statusDot, height: Theme.Size.statusDot)
                .accessibilityHidden(true)
            Image(systemName: member.platform == .windows ? Theme.Symbol.pc : Theme.Symbol.mac)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(member.name)
                    .fixedSize(horizontal: false, vertical: true)
                Text(statusKey)
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
            }
            Spacer()
            if !member.isSelf {
                Button(action: remove) {
                    Image(systemName: Theme.Symbol.remove)
                }
                .buttonStyle(.borderless)
                .help(Text("group.member.remove"))
                .accessibilityLabel(Text("group.member.remove"))
            }
        }
        .accessibilityElement(children: .combine)
    }

    private var statusKey: LocalizedStringKey {
        if member.isSelf { return "group.member.this" }
        if !member.isConnected { return "group.member.offline" }
        return isResponsive ? "group.member.connected" : "group.member.unresponsive"
    }

    private var statusColor: Color {
        if member.isSelf || (member.isConnected && isResponsive) { return Theme.Colors.positive }
        return member.isConnected ? Theme.Colors.warning : Theme.Colors.inactive
    }
}

private struct NearbyRow: View {
    let computer: NearbyComputer
    let pair: () -> Void

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: Theme.Spacing.medium) {
            Image(systemName: computer.platform == .windows ? Theme.Symbol.pc : Theme.Symbol.mac)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(computer.name)
                    .fixedSize(horizontal: false, vertical: true)
                if computer.needsUpdate {
                    Text("group.nearby.needsUpdate")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.warning)
                        .fixedSize(horizontal: false, vertical: true)
                } else if computer.inAnotherGroup {
                    Text("group.nearby.anotherGroup")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Spacer()
            Button("group.nearby.pair", action: pair)
                .disabled(computer.needsUpdate)
        }
    }
}

private struct LocalNetworkBanner: View {
    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Label("group.localNetwork.denied", systemImage: Theme.Symbol.warning)
                .foregroundStyle(Theme.Colors.warning)
                .fixedSize(horizontal: false, vertical: true)
            Button("group.localNetwork.open") { controller.openLocalNetworkSettings() }
        }
        .font(Theme.Typography.caption)
    }
}

private struct RemovedBanner: View {
    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Label("group.removed", systemImage: Theme.Symbol.warning)
                .foregroundStyle(Theme.Colors.warning)
                .fixedSize(horizontal: false, vertical: true)
            Button("group.dismiss") { controller.dismissRemoval() }
        }
        .font(Theme.Typography.caption)
    }
}

/// Input control: who controls this Mac, the lock, and why the cursor came back.
private struct ControlStatus: View {
    let input: InputController
    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            if let by = input.controller {
                Label("control.controlledBy \(controller.name(of: by))", systemImage: Theme.Symbol.controlled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if input.isLocked {
                Label("control.locked", systemImage: Theme.Symbol.locked)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let notice = input.notice {
                Label(message(notice), systemImage: Theme.Symbol.warning)
                    .foregroundStyle(Theme.Colors.warning)
                    .fixedSize(horizontal: false, vertical: true)
                Button("clipboard.dismiss") { input.dismissNotice() }
            }
            if input.isWatching, controller.members.contains(where: { !$0.isSelf && $0.isConnected }) {
                Text("control.hint")
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .font(Theme.Typography.caption)
    }

    private func message(_ notice: ControlNotice) -> LocalizedStringKey {
        switch notice {
        case .refused(let name, .locked): "control.refused.locked \(name)"
        case .refused(let name, .asleep): "control.refused.asleep \(name)"
        case .refused(let name, .notPermitted): "control.refused.notPermitted \(name)"
        case .refused(let name, .busy): "control.refused.busy \(name)"
        case .lost(let name): "control.lost \(name)"
        }
    }
}

private struct ClipboardBanner: View {
    let clipboard: ClipboardController
    let notice: ClipboardNotice

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Label(message, systemImage: Theme.Symbol.warning)
                .foregroundStyle(Theme.Colors.warning)
                .fixedSize(horizontal: false, vertical: true)
            Button("clipboard.dismiss") { clipboard.dismissNotice() }
        }
        .font(Theme.Typography.caption)
    }

    private var message: LocalizedStringKey {
        switch notice {
        case .unavailable(let name): "clipboard.unavailable \(name)"
        case .tooLarge(let name): "clipboard.tooLarge \(name)"
        }
    }
}

extension GroupStartFailure {
    var messageKey: LocalizedStringKey {
        switch self {
        case .keyStore: "group.start.keyStore"
        case .newerVersion: "group.start.newerVersion"
        case .mesh: "group.start.mesh"
        }
    }
}
