import AppKit
import Carbon.HIToolbox
import SwiftUI

/// A shortcut field: click, press the new combination (Esc cancels).
struct HotkeyRecorder: View {
    let hotkey: Hotkey
    let record: (Hotkey) -> Void
    @State private var recording = false
    @State private var monitor: Any?
    @State private var rejected = false

    var body: some View {
        Button {
            recording ? stop() : start()
        } label: {
            Group {
                if recording {
                    Text("settings.hotkey.recording")
                } else {
                    Text(verbatim: hotkey.displayText)
                }
            }
            .frame(minWidth: Theme.Size.hotkeyField)
        }
        .accessibilityLabel(Text("settings.hotkey.accessibility \(hotkey.displayText)"))
        .accessibilityHint(Text("settings.hotkey.hint"))
        .onDisappear(perform: stop)
        if rejected {
            Text("settings.hotkey.needsModifier")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.error)
        }
    }

    private func start() {
        recording = true
        rejected = false
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if event.keyCode == UInt16(kVK_Escape) {
                stop()
            } else if let pressed = Hotkey(keyCode: event.keyCode, flags: event.modifierFlags) {
                record(pressed)
                stop()
            } else {
                rejected = true
            }
            return nil  // the key press is for the recorder, not the window
        }
    }

    private func stop() {
        recording = false
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }
}
