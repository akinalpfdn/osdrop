import SwiftUI

/// Osdrop itself; `OsdropMain` starts it, unless the app hosts the unit tests.
struct OsdropApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    private let dependencies: AppDependencies
    private let pairingWindow: PairingWindowPresenter
    private let clipboardWindow: ClipboardFetchPresenter
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
        dependencies.corners.start()
        dependencies.awdl.start()
        self.dependencies = dependencies
        pairingWindow = PairingWindowPresenter(controller: dependencies.group)
        clipboardWindow = ClipboardFetchPresenter(group: dependencies.group)
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
                launchAtLogin: SystemLaunchAtLogin(), logs: UnifiedLogExporter(), showPermissions: onboarding.show,
                awdl: dependencies.awdl))
        let menu = MenuOpener(settings: dependencies.settings)
        AppDelegate.onLaunch = {
            if onboardingModel.needsAttention {
                // After the permissions window, not under it: the menu closes when a window takes focus.
                onboarding.onNextClose = { menu.openAtFirstLaunch() }
                onboarding.show()
            } else {
                menu.openAtFirstLaunch()
            }
            Task { await updates.checkIfDue() }
        }
        AppDelegate.onReopen = { menu.open() }
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

        Window("transfers.title", id: TransfersWindowView.windowID) {
            TransfersWindowView(group: dependencies.group)
        }
        .defaultSize(width: Theme.Size.transfersWindowWidth, height: Theme.Size.transfersWindowHeight)

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

/// Runs the launch work once AppKit is ready to show windows, and opens the menu when Osdrop is
/// opened again while it runs (from Finder, Launchpad or Spotlight).
final class AppDelegate: NSObject, NSApplicationDelegate {
    @MainActor static var onLaunch: (() -> Void)?
    @MainActor static var onReopen: (() -> Void)?

    func applicationDidFinishLaunching(_ notification: Notification) {
        MainActor.assumeIsolated {
            Self.onLaunch?()
            Self.onLaunch = nil
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        MainActor.assumeIsolated { Self.onReopen?() }
        return false  // the menu is the answer; no window of a scene opens
    }
}
