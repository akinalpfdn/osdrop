import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The Settings window's model: changes are saved and reach core, chords stay distinct, problems
/// show as messages, and diagnostics leave names out.
@MainActor
@Suite("Settings")
struct SettingsViewModelTests {
    private let fixture = GroupFixture()
    private let launch = FakeLaunchAtLogin()

    private func make(
        _ group: GroupController, copy: @escaping @MainActor (String) -> Void = { _ in }
    )
        -> SettingsViewModel
    {
        SettingsViewModel(
            settings: group.settings, group: group,
            updates: UpdatesViewModel(settings: group.settings, checker: NoUpdates()), launchAtLogin: launch,
            logs: FakeLogs(), showPermissions: {}, copy: copy)
    }

    @Test("Input settings are saved as they change")
    func savesInputSettings() {
        let group = fixture.start()
        let viewModel = make(group)
        viewModel.modifierMapping = .positions
        viewModel.pushSeconds = 0.5
        viewModel.immediateCrossing = true
        viewModel.crossingModifier = .option
        #expect(
            group.settings.input
                == InputSettings(
                    modifierMapping: .positions, pushMilliseconds: 500, immediate: true, crossingModifiers: 0b0100_0100)
        )
        let member = GroupFixture.member(GroupFixture.pc)
        viewModel.setPointerSpeed(1.5, of: member)
        #expect(group.settings.pointerSpeeds[GroupFixture.pc] == 1_500)
        #expect(viewModel.pointerSpeed(of: member) == 1.5)
    }

    @Test("Both chords stay distinct, and reset brings back the defaults")
    func hotkeysStayDistinct() {
        let group = fixture.start()
        let viewModel = make(group)
        viewModel.recordLockHotkey(Hotkey.defaultReturnHome)
        #expect(viewModel.message != nil)
        #expect(group.settings.hotkeys == CaptureHotkeys())
        let chord = Hotkey(keyCode: 40, modifiers: [.control, .option])
        viewModel.recordLockHotkey(chord)
        #expect(viewModel.message == nil)
        #expect(group.settings.hotkeys.lockInput == chord)
        viewModel.resetHotkeys()
        #expect(group.settings.hotkeys == CaptureHotkeys())
    }

    @Test("A permission switch reaches the mesh for that member")
    func permissionsReachTheMesh() async throws {
        let group = fixture.start()
        let viewModel = make(group)
        let member = GroupFixture.member(GroupFixture.pc)
        viewModel.setPermission(.files, of: member, to: false)
        try await waitUntil {
            fixture.mesh.calls.contains(
                .permissions(MemberPermissions(control: true, files: false, clipboard: true), GroupFixture.pc))
        }
        #expect(viewModel.permission(.files, of: member))  // the member as shown until the list refreshes
    }

    @Test("A computer dropped in the editor snaps and the whole arrangement goes to the mesh")
    func arrangementReachesTheMesh() async throws {
        let monitor = ScreenMonitor(
            key: [UInt8](repeating: 1, count: 16), frame: CoordRect(x: 0, y: 0, width: 16_000, height: 12_800),
            native: CoordRect(x: 0, y: 0, width: 16_000, height: 12_800), pixelWidth: 2000, pixelHeight: 1600,
            isPrimary: true)
        fixture.mesh.setComputers([
            LayoutComputer(
                deviceID: GroupFixture.laptop,
                placement: Placement(deviceID: GroupFixture.laptop, x: 0, y: 0, isDetached: false), monitors: [monitor]),
            LayoutComputer(
                deviceID: GroupFixture.pc,
                placement: Placement(deviceID: GroupFixture.pc, x: 16_000, y: 0, isDetached: false),
                monitors: [monitor]),
        ])
        let group = fixture.start()
        try await waitUntil { group.arrangement.count == 2 }
        let viewModel = make(group)
        viewModel.move(GroupFixture.pc, droppedAt: CGPoint(x: -17_000, y: 1_600))
        try await waitUntil { fixture.mesh.calls.contains { if case .setLayout = $0 { true } else { false } } }
        #expect(
            fixture.mesh.calls.last
                == .setLayout([
                    Placement(deviceID: GroupFixture.laptop, x: 0, y: 0, isDetached: false),
                    Placement(deviceID: GroupFixture.pc, x: -16_000, y: 1_600, isDetached: false),
                ]))
        #expect(viewModel.arrangementProblem == nil)
    }

    @Test("Launch at login refused by macOS shows a message")
    func launchAtLoginFailure() {
        let group = fixture.start()
        let viewModel = make(group)
        launch.refuses = true
        viewModel.setLaunchesAtLogin(true)
        #expect(viewModel.message != nil)
        #expect(!viewModel.launchesAtLogin)
        launch.refuses = false
        viewModel.setLaunchesAtLogin(true)
        #expect(viewModel.message == nil)
        #expect(viewModel.launchesAtLogin)
    }

    @Test("Diagnostics leave this computer's and the members' names out")
    func diagnosticsLeaveNamesOut() async {
        let group = fixture.start()
        var text = ""
        let viewModel = make(group) { text = $0 }
        await viewModel.copyDiagnostics()
        #expect(viewModel.diagnosticsCopied)
        #expect(text.hasPrefix("Osdrop "))
        #expect(!text.contains("Test Mac"))
    }

    @Test("Every core error has a message of its own kind")
    func errorMessages() {
        #expect(ErrorMessages.key(.invalidLayout) == "error.invalidLayout")
        #expect(ErrorMessages.key(.badMagic) == "error.internal")
        #expect(ErrorMessages.key(.unknown(code: 999)) == "error.internal")
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

@MainActor
final class FakeLaunchAtLogin: LaunchAtLoginManaging {
    var refuses = false
    private(set) var isEnabled = false
    let needsApproval = false

    func setEnabled(_ enabled: Bool) throws(LaunchAtLoginError) {
        if refuses { throw .refused("not in Applications") }
        isEnabled = enabled
    }
}

struct FakeLogs: LogExporting {
    var folder: URL { FileManager.default.temporaryDirectory }
    func export() async throws(LogExportError) -> URL { folder.appending(path: "Osdrop.log") }
    func recentLines(limit: Int) async throws(LogExportError) -> [String] { ["Test Mac connected"] }
}

struct NoUpdates: UpdateChecking {
    func check(current: ReleaseVersion) async -> UpdateCheckOutcome { .upToDate }
}
