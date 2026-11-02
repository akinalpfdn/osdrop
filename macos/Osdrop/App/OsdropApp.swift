import SwiftUI

@main
struct OsdropApp: App {
    var body: some Scene {
        MenuBarExtra {
            MenuContentView()
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
