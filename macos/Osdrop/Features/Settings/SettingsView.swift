import CoreBridge
import SwiftUI

/// The Settings window: one tab per DEVPLAN group.
struct SettingsView: View {
    let viewModel: SettingsViewModel

    var body: some View {
        TabView {
            GeneralSettings(viewModel: viewModel, updates: viewModel.updates)
                .tabItem { Label("settings.general", systemImage: Theme.Symbol.general) }
            ArrangementSettings(viewModel: viewModel)
                .tabItem { Label("settings.arrangement", systemImage: Theme.Symbol.arrangement) }
            DeviceSettings(viewModel: viewModel, group: viewModel.group)
                .tabItem { Label("settings.devices", systemImage: Theme.Symbol.devices) }
            InputSettingsTab(viewModel: viewModel)
                .tabItem { Label("settings.input", systemImage: Theme.Symbol.input) }
            ClipboardSettings(settings: viewModel.settings)
                .tabItem { Label("settings.clipboard", systemImage: Theme.Symbol.clipboard) }
            AdvancedSettings(viewModel: viewModel)
                .tabItem { Label("settings.advanced", systemImage: Theme.Symbol.advanced) }
        }
        .frame(width: Theme.Size.settingsWidth, height: Theme.Size.settingsHeight)
        .background(SettingsWindowReader())
        .onAppear(perform: viewModel.appeared)
    }
}

/// A caption under a setting; it wraps and never truncates.
private struct Note: View {
    let text: LocalizedStringKey

    init(_ text: LocalizedStringKey) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(Theme.Typography.caption)
            .foregroundStyle(Theme.Colors.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct Problem: View {
    let text: String?

    var body: some View {
        if let text {
            Text(verbatim: text)
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.error)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

// MARK: - General

private struct GeneralSettings: View {
    let viewModel: SettingsViewModel
    let updates: UpdatesViewModel

    var body: some View {
        Form {
            Toggle(
                "settings.launchAtLogin",
                isOn: Binding(get: { viewModel.launchesAtLogin }, set: { viewModel.setLaunchesAtLogin($0) }))
            if viewModel.launchNeedsApproval {
                Note("settings.launchAtLogin.approval")
            }
            LabeledContent("settings.permissions") {
                Button("settings.permissions.open", action: viewModel.openPermissions)
            }
            Problem(text: viewModel.message)
            Section("settings.updates") {
                Toggle(
                    "settings.updates.daily",
                    isOn: Binding(
                        get: { viewModel.settings.checksForUpdates },
                        set: { viewModel.settings.checksForUpdates = $0 }))
                LabeledContent("settings.updates.version \(updates.currentVersion.description)") {
                    Button("settings.updates.check") { Task { await updates.checkNow() } }
                        .disabled(updates.isChecking)
                }
                switch updates.result {
                case .checking: Note("settings.updates.checking")
                case .upToDate: Note("settings.updates.upToDate")
                case .available(let version):
                    LabeledContent("settings.updates.available \(version.description)") {
                        Button("settings.updates.download", action: updates.openDownload)
                    }
                case .failed: Note("settings.updates.failed")
                case nil: EmptyView()
                }
            }
        }
        .formStyle(.grouped)
    }
}

// MARK: - Arrangement

private struct ArrangementSettings: View {
    let viewModel: SettingsViewModel

    var body: some View {
        Form {
            Section {
                if viewModel.arrangement.computers.isEmpty {
                    Note("settings.arrangement.empty")
                } else {
                    ArrangementEditor(geometry: viewModel.arrangement, move: viewModel.move)
                }
                Note("settings.arrangement.description")
                Problem(text: viewModel.arrangementProblem)
            }
        }
        .formStyle(.grouped)
    }
}

// MARK: - Devices

private struct DeviceSettings: View {
    let viewModel: SettingsViewModel
    let group: GroupController
    @State private var name = ""

    var body: some View {
        Form {
            Section("settings.devices.thisComputer") {
                LabeledContent("settings.devices.name") {
                    TextField("settings.devices.name", text: $name)
                        .labelsHidden()
                        .frame(width: Theme.Size.nameField)
                        .onSubmit { viewModel.rename(to: name) }
                }
                Note("settings.devices.name.description")
            }
            Section("settings.devices.members") {
                let others = group.members.filter { !$0.isSelf }
                if others.isEmpty {
                    Note("group.empty")
                }
                ForEach(others) { member in
                    MemberSettings(viewModel: viewModel, member: member) { group.revoke(member) }
                }
                if !group.members.isEmpty {
                    Button("group.leave", role: .destructive) { group.leaveGroup() }
                }
            }
            Section("group.nearby") {
                if group.nearby.isEmpty {
                    Note("group.nearby.none")
                }
                ForEach(group.nearby) { computer in
                    LabeledContent {
                        Button("settings.devices.pair") { group.pair(with: computer) }
                            .disabled(computer.inAnotherGroup || computer.needsUpdate)
                    } label: {
                        Text(verbatim: computer.name)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { name = viewModel.thisComputerName }
    }
}

private struct MemberSettings: View {
    let viewModel: SettingsViewModel
    let member: GroupMember
    let revoke: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            HStack {
                Label {
                    Text(verbatim: member.name)
                } icon: {
                    Image(systemName: member.platform == .windows ? Theme.Symbol.pc : Theme.Symbol.mac)
                }
                Text(member.isConnected ? "group.member.connected" : "group.member.offline")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                Spacer()
                Button("settings.devices.remove", role: .destructive, action: revoke)
            }
            permission(.control, "settings.devices.allowControl")
            permission(.files, "settings.devices.allowFiles")
            permission(.clipboard, "settings.devices.allowClipboard")
        }
    }

    private func permission(_ kind: PermissionKind, _ title: LocalizedStringKey) -> some View {
        Toggle(
            title,
            isOn: Binding(
                get: { viewModel.permission(kind, of: member) },
                set: { viewModel.setPermission(kind, of: member, to: $0) }))
    }
}

// MARK: - Input

private struct InputSettingsTab: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        Form {
            Section("settings.input.crossing") {
                Toggle("settings.input.immediate", isOn: $viewModel.immediateCrossing)
                if !viewModel.immediateCrossing {
                    LabeledContent(
                        "settings.input.push \(viewModel.pushSeconds.formatted(.number.precision(.fractionLength(1))))"
                    ) {
                        Slider(value: $viewModel.pushSeconds, in: Tunables.Control.pushSecondsRange, step: 0.1)
                            .frame(width: Theme.Size.nameField)
                    }
                }
                Picker("settings.input.modifier", selection: $viewModel.crossingModifier) {
                    ForEach(CrossingModifier.allCases) { modifier in
                        Text(modifier.titleKey).tag(modifier)
                    }
                }
            }
            Section("settings.input.keys") {
                Picker("settings.input.mapping", selection: $viewModel.modifierMapping) {
                    Text("settings.input.mapping.shortcuts").tag(ModifierMapping.shortcuts)
                    Text("settings.input.mapping.positions").tag(ModifierMapping.positions)
                }
                Note("settings.input.mapping.description")
            }
            Section("settings.input.speed") {
                let others = viewModel.group.members.filter { !$0.isSelf }
                if others.isEmpty {
                    Note("group.empty")
                }
                ForEach(others) { member in
                    LabeledContent {
                        Slider(
                            value: Binding(
                                get: { viewModel.pointerSpeed(of: member) },
                                set: { viewModel.setPointerSpeed($0, of: member) }),
                            in: Tunables.Control.speedRange
                        )
                        .frame(width: Theme.Size.nameField)
                    } label: {
                        Text(verbatim: member.name)
                    }
                }
            }
            Section("settings.input.hotkeys") {
                LabeledContent("settings.input.returnHotkey") {
                    HotkeyRecorder(hotkey: viewModel.settings.hotkeys.returnHome, record: viewModel.recordReturnHotkey)
                }
                LabeledContent("settings.input.lockHotkey") {
                    HotkeyRecorder(hotkey: viewModel.settings.hotkeys.lockInput, record: viewModel.recordLockHotkey)
                }
                Button("settings.hotkey.reset", action: viewModel.resetHotkeys)
                    .buttonStyle(.link)
                Problem(text: viewModel.message)
            }
        }
        .formStyle(.grouped)
    }
}

extension CrossingModifier {
    var titleKey: LocalizedStringKey {
        switch self {
        case .none: "settings.input.modifier.none"
        case .control: "settings.input.modifier.control"
        case .option: "settings.input.modifier.option"
        case .command: "settings.input.modifier.command"
        case .shift: "settings.input.modifier.shift"
        }
    }
}

// MARK: - Clipboard and files

private struct ClipboardSettings: View {
    @Bindable var settings: AppSettings

    var body: some View {
        Form {
            Section("settings.clipboard.title") {
                Toggle("settings.clipboard.share", isOn: $settings.sharesClipboard)
                Note("settings.clipboard.share.description")
                Picker("settings.clipboard.filesLimit", selection: $settings.clipboardFilesLimit) {
                    ForEach(Tunables.Clipboard.filesLimitChoices, id: \.self) { limit in
                        Text(verbatim: ByteCountFormatter.string(fromByteCount: Int64(limit), countStyle: .file))
                            .tag(limit)
                    }
                }
            }
            Section("settings.files.title") {
                Toggle("settings.files.mark", isOn: $settings.marksReceivedFiles)
                Note("settings.files.mark.description")
            }
        }
        .formStyle(.grouped)
    }
}

// MARK: - Advanced

private struct AdvancedSettings: View {
    let viewModel: SettingsViewModel

    var body: some View {
        Form {
            Section("settings.diagnostics") {
                LabeledContent("settings.diagnostics.copy.title") {
                    Button("settings.diagnostics.copy") { Task { await viewModel.copyDiagnostics() } }
                }
                if viewModel.diagnosticsCopied {
                    Note("settings.diagnostics.copied")
                }
                Note("settings.diagnostics.description")
                LabeledContent("settings.logs.title") {
                    Button("settings.logs.export") { Task { await viewModel.exportLog() } }
                }
                Problem(text: viewModel.message)
            }
        }
        .formStyle(.grouped)
    }
}
