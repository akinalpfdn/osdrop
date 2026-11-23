import AppKit
import SwiftUI

/// Shows the onboarding view in its own window; a menu bar app has no main window to host it.
@MainActor
final class OnboardingWindow {
    private var window: NSWindow?
    private var closeObserver: (any NSObjectProtocol)?
    private let viewModel: OnboardingViewModel

    /// Runs once, the next time the window closes, by its button or its close box.
    var onNextClose: (() -> Void)?

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
        // No queue: AppKit closes windows on the main thread, and the block runs right there.
        closeObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: nil
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.closing() }
        }
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
        self.window = window
    }

    func close() {
        window?.close()
        window = nil
        if let closeObserver {
            NotificationCenter.default.removeObserver(closeObserver)
        }
        closeObserver = nil
    }

    private func closing() {
        let action = onNextClose
        onNextClose = nil
        action?()
    }
}
