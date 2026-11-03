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
            Label("app.name", image: Theme.Artwork.menuBar)
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
