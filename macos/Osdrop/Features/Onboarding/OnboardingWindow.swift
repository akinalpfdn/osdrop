import AppKit
import SwiftUI

/// Shows the onboarding view in its own window; a menu bar app has no main window to host it.
@MainActor
final class OnboardingWindow {
    private var window: NSWindow?
    private let viewModel: OnboardingViewModel

    init(viewModel: OnboardingViewModel) {
        self.viewModel = viewModel
    }

    func show() {
        if let window {
            NSApp.activate()
            window.makeKeyAndOrderFront(nil)
            window.orderFrontRegardless()  // a menu bar app is not active by itself
            return
        }
        let view = OnboardingView(viewModel: viewModel) { [weak self] in self?.close() }
        let window = NSWindow(
            contentRect: .zero, styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = String(localized: "onboarding.windowTitle")
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: view)
        window.setContentSize(window.contentView?.fittingSize ?? .zero)
        window.center()
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
        self.window = window
    }

    func close() {
        window?.close()
        window = nil
    }
}
