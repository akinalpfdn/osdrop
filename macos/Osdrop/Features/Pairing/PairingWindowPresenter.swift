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

    private func update() {
        guard controller.pairing != nil else {
            panel?.orderOut(nil)
            return
        }
        let panel = self.panel ?? makePanel()
        self.panel = panel
        if !panel.isVisible {
            panel.center()
            NSApp.activate()
            panel.makeKeyAndOrderFront(nil)
        }
    }

    private func makePanel() -> NSPanel {
        let hosting = NSHostingController(rootView: PairingView(controller: controller))
        hosting.sizingOptions = .preferredContentSize
        let panel = NSPanel(contentViewController: hosting)
        panel.styleMask = [.titled, .closable]
        panel.title = String(localized: "pairing.title")
        panel.level = .floating
        panel.isReleasedWhenClosed = false
        panel.delegate = self
        return panel
    }

    func windowWillClose(_ notification: Notification) {
        controller.dismissPairing()
    }
}
