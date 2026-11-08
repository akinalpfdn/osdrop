import SwiftUI

@main
struct OsdropApp: App {
    private let dependencies: AppDependencies
    private let pairingWindow: PairingWindowPresenter
    private let systemEvents: SystemEvents
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
        #if DEBUG
            _developer = State(initialValue: DeveloperInputViewModel(dependencies: dependencies))
            _developerDrag = State(initialValue: DeveloperDragViewModel(dependencies: dependencies))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            MenuContentView(accessibility: dependencies.accessibility, group: dependencies.group)
        } label: {
            MenuBarLabel(group: dependencies.group)
        }
        .menuBarExtraStyle(.window)

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
