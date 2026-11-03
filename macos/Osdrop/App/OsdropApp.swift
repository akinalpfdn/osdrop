import SwiftUI

@main
struct OsdropApp: App {
    private let dependencies: AppDependencies
    #if DEBUG
        @State private var developer: DeveloperInputViewModel
        @State private var developerDrag: DeveloperDragViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        dependencies.accessibility.startMonitoring()
        // No drag can be running yet, so anything staged belongs to an earlier run.
        dependencies.staging.removeAll()
        self.dependencies = dependencies
        #if DEBUG
            _developer = State(initialValue: DeveloperInputViewModel(dependencies: dependencies))
            _developerDrag = State(initialValue: DeveloperDragViewModel(dependencies: dependencies))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            MenuContentView(accessibility: dependencies.accessibility)
        } label: {
            Label("app.name", image: Theme.Artwork.menuBar)
        }
        .menuBarExtraStyle(.window)

        #if DEBUG
            Window("developer.title", id: DeveloperInputView.windowID) {
                DeveloperInputView(viewModel: developer, drag: developerDrag)
            }
            .windowResizability(.contentSize)
        #endif
    }
}
