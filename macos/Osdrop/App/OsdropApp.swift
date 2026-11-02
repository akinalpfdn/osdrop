import SwiftUI

@main
struct OsdropApp: App {
    private let dependencies: AppDependencies
    #if DEBUG
        @State private var developer: DeveloperInputViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        dependencies.accessibility.startMonitoring()
        self.dependencies = dependencies
        #if DEBUG
            _developer = State(initialValue: DeveloperInputViewModel(dependencies: dependencies))
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
                DeveloperInputView(viewModel: developer)
            }
            .windowResizability(.contentSize)
        #endif
    }
}
