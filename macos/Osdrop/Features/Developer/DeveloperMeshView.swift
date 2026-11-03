import CoreBridge
import SwiftUI

/// The mesh as it runs (debug builds): this computer, its group and every member's session.
struct DeveloperMeshView: View {
    static let windowID = "developer-mesh"

    let controller: GroupController

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Group {
                if let device = controller.deviceID {
                    Text("developer.mesh.device \(device.hex)")
                }
                if let group = controller.groupID {
                    Text("developer.mesh.group \(group.hex)")
                } else {
                    Text("developer.mesh.noGroup")
                }
                Text("developer.mesh.port \(Int(controller.listenPort))")
            }
            .font(Theme.Typography.monospaced)
            .textSelection(.enabled)
            Divider()
            if controller.members.isEmpty {
                Text("developer.mesh.noMembers")
                    .foregroundStyle(Theme.Colors.secondary)
            }
            ForEach(controller.members) { member in
                SessionRow(
                    member: member, statistics: controller.statistics[member.deviceID],
                    isResponsive: !controller.unresponsive.contains(member.deviceID))
            }
        }
        .padding(Theme.Spacing.section)
        .frame(width: Theme.Size.developerWindowWidth, alignment: .leading)
    }
}

private struct SessionRow: View {
    let member: GroupMember
    let statistics: SessionStatistics?
    let isResponsive: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Text(member.name)
                .font(Theme.Typography.title)
            Text(member.deviceID.hex)
                .font(Theme.Typography.monospaced)
                .foregroundStyle(Theme.Colors.secondary)
                .textSelection(.enabled)
            if member.isSelf {
                Text("group.member.this")
            } else if !member.isConnected {
                Text("developer.mesh.noSession")
            } else if let statistics {
                Text("developer.mesh.rtt \(Self.milliseconds(statistics.roundTripMicroseconds))")
                Text("developer.mesh.offset \(statistics.clockOffsetMicroseconds)")
                Text("developer.mesh.rejected \(Int64(clamping: statistics.datagramsRejected))")
                Text("developer.mesh.stale \(Int64(clamping: statistics.staleMotionDropped))")
                if !isResponsive {
                    Text("group.member.unresponsive")
                        .foregroundStyle(Theme.Colors.warning)
                }
            } else {
                Text("group.member.connected")
            }
        }
        .font(Theme.Typography.caption)
    }

    /// Microseconds as milliseconds with one decimal.
    private static func milliseconds(_ microseconds: UInt32) -> String {
        (Double(microseconds) / 1000).formatted(.number.precision(.fractionLength(1)))
    }
}
