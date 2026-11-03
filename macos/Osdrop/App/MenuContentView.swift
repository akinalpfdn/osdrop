import AppKit
import CoreBridge
import SwiftUI

struct MenuContentView: View {
    let accessibility: AccessibilityPermission
    let group: GroupController
    private let appVersion = AppVersion.current
    private let coreVersion = CoreVersion.linked
    #if DEBUG
        @Environment(\.openWindow) private var openWindow
    #endif

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
            .foregroundStyle(Theme.Colors.secondary)
            .fixedSize(horizontal: false, vertical: true)

            if !accessibility.isTrusted {
                AccessibilityBanner(accessibility: accessibility)
            }

            Divider()
            GroupMenuSection(controller: group)

            Divider()
            #if DEBUG
                Button("menu.developer") {
                    openWindow(id: DeveloperInputView.windowID)
                    NSApp.activate()
                }
                Button("menu.developer.mesh") {
                    openWindow(id: DeveloperMeshView.windowID)
                    NSApp.activate()
                }
            #endif
            Button("menu.quit") {
                NSApplication.shared.terminate(nil)
            }
            .keyboardShortcut("q")
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.menuWidth, alignment: .leading)
    }
}

private struct AccessibilityBanner: View {
    let accessibility: AccessibilityPermission

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Label("menu.accessibility.missing", systemImage: Theme.Symbol.warning)
                .foregroundStyle(Theme.Colors.warning)
                .fixedSize(horizontal: false, vertical: true)
            Button("menu.accessibility.open") {
                accessibility.requestAccess()
                accessibility.openSystemSettings()
            }
        }
        .font(Theme.Typography.caption)
    }
}
