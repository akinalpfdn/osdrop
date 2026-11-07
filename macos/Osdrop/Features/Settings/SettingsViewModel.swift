import AppKit
import CoreBridge
import Foundation
import Observation

/// The modifier the cursor needs to cross, as Settings offers it.
enum CrossingModifier: CaseIterable, Identifiable {
    case none
    case control
    case option
    case command
    case shift

    var id: Self { self }

    /// Enter modifier bits, left and right keys.
    var bits: UInt8 {
        switch self {
        case .none: 0
        case .control: 0b0001_0001
        case .shift: 0b0010_0010
        case .option: 0b0100_0100
        case .command: 0b1000_1000
        }
    }

    init(bits: UInt8) {
        self = Self.allCases.first { $0.bits == bits } ?? .none
    }
}

/// What a member may do on this computer, one switch each.
enum PermissionKind: CaseIterable {
    case control
    case files
    case clipboard
}

/// The Settings window (DEVPLAN Phase 16): General, Arrangement, Devices, Input, Clipboard and
/// Files, Advanced. Every change is saved at once and reaches core at once.
@MainActor
@Observable
final class SettingsViewModel {
    let settings: AppSettings
    let group: GroupController
    let updates: UpdatesViewModel
    private(set) var launchesAtLogin: Bool
    private(set) var launchNeedsApproval: Bool
    /// The last problem with a setting, shown under it.
    private(set) var message: String?
    private(set) var arrangementProblem: String?
    private(set) var exportedLog: URL?
    /// "Copied" appears under the button after Copy diagnostics.
    private(set) var diagnosticsCopied = false

    @ObservationIgnored private let launchAtLogin: any LaunchAtLoginManaging
    @ObservationIgnored private let logs: any LogExporting
    @ObservationIgnored private let showPermissions: @MainActor () -> Void
    @ObservationIgnored private let copy: @MainActor (String) -> Void

    init(
        settings: AppSettings, group: GroupController, updates: UpdatesViewModel,
        launchAtLogin: any LaunchAtLoginManaging, logs: any LogExporting,
        showPermissions: @escaping @MainActor () -> Void,
        copy: @escaping @MainActor (String) -> Void = SettingsViewModel.copyToPasteboard
    ) {
        self.settings = settings
        self.group = group
        self.updates = updates
        self.launchAtLogin = launchAtLogin
        self.logs = logs
        self.showPermissions = showPermissions
        self.copy = copy
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
    }

    func appeared() {
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
        diagnosticsCopied = false
    }

    // MARK: General

    func setLaunchesAtLogin(_ enabled: Bool) {
        do throws(LaunchAtLoginError) {
            try launchAtLogin.setEnabled(enabled)
            message = nil
        } catch {
            message = String(localized: "settings.launchAtLogin.failed")
        }
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
    }

    func openPermissions() {
        showPermissions()
    }

    // MARK: Arrangement

    var arrangement: ArrangementGeometry {
        ArrangementGeometry(layout: group.arrangement, members: group.members)
    }

    /// A computer dropped in the editor: it snaps beside the nearest other one, and the new
    /// arrangement goes to every member.
    func move(_ device: DeviceID, droppedAt drop: CGPoint) {
        let offset = arrangement.snapped(device, droppedAt: drop)
        let placements = ArrangementGeometry.placements(of: group.arrangement, moving: device, to: offset)
        Task { [weak self] in
            guard let self else { return }
            let problem = await group.setArrangement(placements)
            arrangementProblem = problem.map(ErrorMessages.text)
        }
    }

    // MARK: Devices

    var thisComputerName: String { group.deviceName }

    func rename(to name: String) {
        group.rename(to: name)
    }

    func permission(_ kind: PermissionKind, of member: GroupMember) -> Bool {
        switch kind {
        case .control: member.permissions.control
        case .files: member.permissions.files
        case .clipboard: member.permissions.clipboard
        }
    }

    func setPermission(_ kind: PermissionKind, of member: GroupMember, to allowed: Bool) {
        var permissions = member.permissions
        switch kind {
        case .control: permissions.control = allowed
        case .files: permissions.files = allowed
        case .clipboard: permissions.clipboard = allowed
        }
        group.setPermissions(permissions, for: member)
    }

    // MARK: Input

    var modifierMapping: ModifierMapping {
        get { settings.input.modifierMapping }
        set { change { $0.modifierMapping = newValue } }
    }

    /// How long a push must last, in seconds.
    var pushSeconds: Double {
        get { Double(settings.input.pushMilliseconds) / 1_000 }
        set { change { $0.pushMilliseconds = UInt32((newValue * 1_000).rounded()) } }
    }

    var immediateCrossing: Bool {
        get { settings.input.immediate }
        set { change { $0.immediate = newValue } }
    }

    var crossingModifier: CrossingModifier {
        get { CrossingModifier(bits: settings.input.crossingModifiers) }
        set { change { $0.crossingModifiers = newValue.bits } }
    }

    /// The pointer speed on `member`, as a share of this Mac's own (1 is the same).
    func pointerSpeed(of member: GroupMember) -> Double {
        Double(settings.pointerSpeeds[member.deviceID] ?? Tunables.Control.normalSpeed) / 1_000
    }

    func setPointerSpeed(_ speed: Double, of member: GroupMember) {
        let permille = UInt32((speed * 1_000).rounded())
        settings.pointerSpeeds[member.deviceID] = permille
        group.input?.setPointerSpeed(permille, for: member.deviceID)
    }

    func recordReturnHotkey(_ hotkey: Hotkey) {
        setHotkeys(CaptureHotkeys(returnHome: hotkey, lockInput: settings.hotkeys.lockInput))
    }

    func recordLockHotkey(_ hotkey: Hotkey) {
        setHotkeys(CaptureHotkeys(returnHome: settings.hotkeys.returnHome, lockInput: hotkey))
    }

    func resetHotkeys() {
        setHotkeys(CaptureHotkeys())
    }

    private func setHotkeys(_ hotkeys: CaptureHotkeys) {
        guard hotkeys.returnHome != hotkeys.lockInput else {
            message = String(localized: "settings.hotkey.taken")
            return
        }
        message = nil
        settings.hotkeys = hotkeys
        group.input?.setHotkeys(hotkeys)
    }

    private func change(_ update: (inout InputSettings) -> Void) {
        var input = settings.input
        update(&input)
        settings.input = input
        group.input?.apply(input)
    }

    // MARK: Advanced

    func copyDiagnostics() async {
        let lines = (try? await logs.recentLines(limit: Tunables.Logs.diagnosticsLines)) ?? []  // a log is optional
        let report = DiagnosticsReport(
            appVersion: AppVersion.current?.description ?? "?", coreVersion: CoreVersion.linked.description,
            system: ProcessInfo.processInfo.operatingSystemVersionString, model: DiagnosticsReport.hardwareModel,
            facts: diagnosticFacts, logLines: lines, privateNames: [group.deviceName] + group.members.map(\.name))
        copy(report.text)
        diagnosticsCopied = true
    }

    func exportLog() async {
        do throws(LogExportError) {
            let file = try await logs.export()
            exportedLog = file
            message = nil
            NSWorkspace.shared.activateFileViewerSelecting([file])
        } catch {
            message = String(localized: "settings.logs.failed")
        }
    }

    /// Settings and state, without names, keys or addresses.
    private var diagnosticFacts: [(String, String)] {
        let input = settings.input
        return [
            ("Members", "\(group.members.count) (\(group.members.filter(\.isConnected).count) connected)"),
            ("Local network", String(describing: group.localNetwork)),
            ("Watching input", String(describing: group.input?.isWatching ?? false)),
            ("Modifier mapping", String(describing: input.modifierMapping)),
            (
                "Push",
                "\(input.pushMilliseconds) ms, immediate \(input.immediate), modifiers \(input.crossingModifiers)"
            ),
            ("Shares clipboard", String(describing: settings.sharesClipboard)),
            ("Marks received files", String(describing: settings.marksReceivedFiles)),
        ]
    }

    static func copyToPasteboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}
