import SwiftUI

@main
struct OsdropApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    private let dependencies: AppDependencies
    private let pairingWindow: PairingWindowPresenter
    private let systemEvents: SystemEvents
    private let onboarding: OnboardingWindow
    @State private var settings: SettingsViewModel
    @State private var updates: UpdatesViewModel
    #if DEBUG
        @State private var developer: DeveloperInputViewModel
        @State private var developerDrag: DeveloperDragViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        dependencies.accessibility.startMonitoring()
        // No drag can be running yet, so anything staged belongs to an earlier run.
        dependencies.staging.removeAll()
        dependencies.group.start()
        self.dependencies = dependencies
        pairingWindow = PairingWindowPresenter(controller: dependencies.group)
        systemEvents = SystemEvents(group: dependencies.group)
        let onboardingModel = OnboardingViewModel(
            accessibility: dependencies.accessibility, localNetworkCheck: BonjourLocalNetworkCheck(),
            settings: dependencies.settings)
        let onboarding = OnboardingWindow(viewModel: onboardingModel)
        self.onboarding = onboarding
        let updates = UpdatesViewModel(settings: dependencies.settings, checker: GitHubUpdateChecker())
        _updates = State(initialValue: updates)
        _settings = State(
            initialValue: SettingsViewModel(
                settings: dependencies.settings, group: dependencies.group, updates: updates,
                launchAtLogin: SystemLaunchAtLogin(), logs: UnifiedLogExporter(), showPermissions: onboarding.show))
        AppDelegate.onLaunch = {
            if onboardingModel.needsAttention { onboarding.show() }
            Task { await updates.checkIfDue() }
        }
        #if DEBUG
            _developer = State(initialValue: DeveloperInputViewModel(dependencies: dependencies))
            _developerDrag = State(initialValue: DeveloperDragViewModel(dependencies: dependencies))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            MenuContentView(
                accessibility: dependencies.accessibility, group: dependencies.group, settings: dependencies.settings,
                updates: updates)
        } label: {
            MenuBarLabel(group: dependencies.group)
        }
        .menuBarExtraStyle(.window)

        Settings {
            SettingsView(viewModel: settings)
        }

        #if DEBUG
            Window("developer.title", id: DeveloperInputView.windowID) {
                DeveloperInputView(viewModel: developer, drag: developerDrag)
            }
            .windowResizability(.contentSize)

            Window("developer.mesh.title", id: DeveloperMeshView.windowID) {
                DeveloperMeshView(controller: dependencies.group)
            }
            .windowResizability(.contentSize)
        #endif
    }
}

/// The menu bar icon; another one while another computer controls this Mac (DEVPLAN Phase 11).
private struct MenuBarLabel: View {
    let group: GroupController

    var body: some View {
        if group.input?.controller != nil {
            Label("app.name", systemImage: Theme.Symbol.controlled)
        } else {
            Label("app.name", image: Theme.Artwork.menuBar)
        }
    }
}

/// Runs the launch work once AppKit is ready to show windows.
final class AppDelegate: NSObject, NSApplicationDelegate {
    @MainActor static var onLaunch: (() -> Void)?

    func applicationDidFinishLaunching(_ notification: Notification) {
        MainActor.assumeIsolated {
            Self.onLaunch?()
            Self.onLaunch = nil
        }
    }
}
