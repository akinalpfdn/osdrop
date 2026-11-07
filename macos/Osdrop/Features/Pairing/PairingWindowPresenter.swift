import AppKit
import Observation
import SwiftUI

/// Shows the pairing window while a pairing is in progress, on either computer: the one that
/// started it and the one that was asked, which may be nobody's menu at that moment.
@MainActor
final class PairingWindowPresenter: NSObject, NSWindowDelegate {
    private let controller: GroupController
    private var panel: NSPanel?

    init(controller: GroupController) {
        self.controller = controller
        super.init()
        observe()
    }

    private func observe() {
        withObservationTracking {
            _ = controller.pairing
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                self?.observe()
            }
        }
        update()
    }

    /// The panel is shown without activating Osdrop and stays in front while another app is
    /// active: the menu that started the pairing closes, and Osdrop goes to the background, at
    /// once (a panel hides when its app is not active unless told otherwise).
    private func update() {
        guard controller.pairing != nil else {
            panel?.orderOut(nil)
            return
        }
        let panel = self.panel ?? makePanel()
        self.panel = panel
        if !panel.isVisible {
            panel.center()
        }
        panel.orderFrontRegardless()
    }

    private func makePanel() -> NSPanel {
        let hosting = NSHostingController(rootView: PairingView(controller: controller))
        hosting.sizingOptions = .preferredContentSize
        // A nonactivating panel must be one from the start.
        let panel = NSPanel(
            contentRect: .zero, styleMask: [.titled, .closable, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.contentViewController = hosting
        panel.title = String(localized: "pairing.title")
        panel.level = .floating
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.delegate = self
        return panel
    }

    /// The pairing window while it is shown, for the tests.
    var window: NSPanel? {
        panel?.isVisible == true ? panel : nil
    }

    func windowWillClose(_ notification: Notification) {
        controller.dismissPairing()
    }
}
