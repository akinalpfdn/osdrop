import AppKit
import CoreBridge
import SwiftUI

/// The menu bar panel (DEVPLAN Phase 16): where the input is, quick toggles, the group, and the
/// way to Settings.
struct MenuContentView: View {
    let accessibility: AccessibilityPermission
    let group: GroupController
    let settings: AppSettings
    let updates: UpdatesViewModel
    #if DEBUG
        @Environment(\.openWindow) private var openWindow
    #endif

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            MenuHeader()

            if !accessibility.isTrusted {
                AccessibilityBanner(accessibility: accessibility)
            }
            if let update = updates.available {
                UpdateBanner(version: update.version.description, open: updates.openDownload)
            }

            if let input = group.input {
                InputStatus(input: input, group: group)
                QuickToggles(input: input, settings: settings)
            }
            if let transfers = group.transfers, !transfers.rows.isEmpty {
                Divider()
                TransfersSection(transfers: transfers)
            }

            Divider()
            GroupMenuSection(controller: group)

            #if DEBUG
                Divider()
                Button("menu.developer") {
                    openWindow(id: DeveloperInputView.windowID)
                    NSApp.activate()
                }
                Button("menu.developer.mesh") {
                    openWindow(id: DeveloperMeshView.windowID)
                    NSApp.activate()
                }
            #endif
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.menuWidth, alignment: .leading)
    }
}

private struct MenuHeader: View {
    @Environment(\.openSettings) private var openSettings
    private let appVersion = AppVersion.current

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text("app.name")
                    .font(Theme.Typography.title)
                Group {
                    if let appVersion {
                        Text("menu.version \(appVersion.description)")
                    } else {
                        Text("menu.versionUnknown")
                    }
                }
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
            }
            .accessibilityElement(children: .combine)
            Spacer()
            // A plain button: SettingsLink swallows the click, so nothing could front its window.
            Button {
                openSettings()
                SettingsWindow.bringToFront()
            } label: {
                Label("menu.settings", systemImage: Theme.Symbol.settings)
                    .labelStyle(.iconOnly)
            }
            .help(Text("menu.settings"))
            Button {
                NSApplication.shared.terminate(nil)
            } label: {
                Label("menu.quit", systemImage: Theme.Symbol.quit)
                    .labelStyle(.iconOnly)
            }
            .help(Text("menu.quit"))
            .keyboardShortcut("q")
        }
    }
}

/// Which computer has this Mac's input, or which computer controls this Mac.
private struct InputStatus: View {
    let input: InputController
    let group: GroupController

    var body: some View {
        Group {
            if let device = input.capturingFor {
                Label("menu.input.elsewhere \(group.name(of: device))", systemImage: Theme.Symbol.input)
            } else if input.controller == nil {
                Label("menu.input.here", systemImage: Theme.Symbol.input)
                    .foregroundStyle(Theme.Colors.secondary)
            }
        }
        .font(Theme.Typography.caption)
        .fixedSize(horizontal: false, vertical: true)
    }
}

private struct QuickToggles: View {
    let input: InputController
    @Bindable var settings: AppSettings

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Toggle(isOn: Binding(get: { input.isLocked }, set: { input.setLocked($0) })) {
                Text("menu.lockInput \(settings.hotkeys.lockInput.displayText)")
            }
            Toggle("menu.shareClipboard", isOn: $settings.sharesClipboard)
        }
        .toggleStyle(.switch)
        .controlSize(.small)
    }
}

private struct UpdateBanner: View {
    let version: String
    let open: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Text("menu.update.available \(version)")
                .fixedSize(horizontal: false, vertical: true)
            Button("menu.update.download", action: open)
        }
        .font(Theme.Typography.caption)
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
