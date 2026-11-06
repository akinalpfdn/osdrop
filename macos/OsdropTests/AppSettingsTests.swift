import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The user's settings as UserDefaults keep them: defaults, limits, and every change saved at once.
@MainActor
@Suite("App settings")
struct AppSettingsTests {
    private let defaults: UserDefaults

    init() throws {
        defaults = try #require(UserDefaults(suiteName: "osdrop-tests-\(UUID())"))
    }

    @Test("Nothing stored gives the defaults")
    func defaultsWhenEmpty() {
        let settings = AppSettings(defaults: defaults)
        #expect(settings.input == InputSettings())
        #expect(settings.input.modifierMapping == .shortcuts, "shortcuts keep their place (user decision)")
        #expect(settings.pointerSpeeds.isEmpty)
        #expect(settings.hotkeys == CaptureHotkeys())
        #expect(settings.checksForUpdates)
        #expect(settings.sharesClipboard)
        #expect(settings.clipboardFilesLimit == Tunables.Clipboard.filesLimit)
        #expect(!settings.marksReceivedFiles, "received files are not marked (user decision)")
    }

    @Test("Changes are saved and read back, within core's limits")
    func savesAndReadsBack() throws {
        let device = try #require(DeviceID(bytes: [UInt8](repeating: 0x42, count: 16)))
        let settings = AppSettings(defaults: defaults)
        settings.input = InputSettings(
            modifierMapping: .positions, pushMilliseconds: 300, immediate: true, crossingModifiers: 17)
        settings.pointerSpeeds = [device: 2_000]
        settings.hotkeys.lockInput = Hotkey(keyCode: 40, modifiers: [.control, .shift])
        settings.sharesClipboard = false
        settings.clipboardFilesLimit = 1024 * 1024 * 1024
        settings.marksReceivedFiles = true

        let reread = AppSettings(defaults: defaults)
        #expect(reread.input == settings.input)
        #expect(reread.pointerSpeeds == [device: 2_000])
        #expect(reread.hotkeys == settings.hotkeys)
        #expect(!reread.sharesClipboard)
        #expect(reread.clipboardFilesLimit == 1024 * 1024 * 1024)
        #expect(reread.marksReceivedFiles)
    }

    @Test("Values written by hand outside the limits fall back")
    func clampsHandWrittenValues() throws {
        let device = try #require(DeviceID(bytes: [UInt8](repeating: 0x42, count: 16)))
        defaults.set(9_000, forKey: "input.pushMilliseconds")
        defaults.set([device.hex: 50, "not a device": 1_000], forKey: "input.pointerSpeeds")
        defaults.set(123, forKey: "clipboardFilesLimit")
        let settings = AppSettings(defaults: defaults)
        #expect(settings.input.pushMilliseconds == InputSettings.maxPushMilliseconds)
        #expect(settings.pointerSpeeds.isEmpty)
        #expect(settings.clipboardFilesLimit == Tunables.Clipboard.filesLimit)
    }
}
