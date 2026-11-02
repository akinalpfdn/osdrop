import AppKit
import CoreBridge
import SwiftUI

struct MenuContentView: View {
    private let appVersion = AppVersion.current
    private let coreVersion = CoreVersion.linked

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text("app.name")
                .font(Theme.Typography.title)
            Group {
                if let appVersion {
                    Text("menu.version \(appVersion.description)")
                } else {
                    Text("menu.versionUnknown")
                }
                Text("menu.coreVersion \(coreVersion.description)")
            }
            .font(Theme.Typography.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
            Divider()
            Button("menu.quit") {
                NSApplication.shared.terminate(nil)
            }
            .keyboardShortcut("q")
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.menuWidth, alignment: .leading)
    }
}
