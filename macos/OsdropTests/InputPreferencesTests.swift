import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The input settings as `defaults write` leaves them, until the settings window (Phase 16).
@Suite("Input preferences")
struct InputPreferencesTests {
    private let defaults: UserDefaults
    private let preferences: UserDefaultsInputPreferences

    init() throws {
        defaults = try #require(UserDefaults(suiteName: "osdrop-tests-\(UUID())"))
        preferences = UserDefaultsInputPreferences(defaults: defaults)
    }

    @Test("Nothing stored gives the defaults: shortcuts keep their place, a one-second push")
    func defaultsWhenEmpty() {
        #expect(preferences.settings() == InputSettings())
        #expect(preferences.settings().modifierMapping == .shortcuts)
        #expect(preferences.pointerSpeeds().isEmpty)
    }

    @Test("Stored values are read and kept within core's limits")
    func readsStoredValues() throws {
        let keys = Tunables.Settings.Input.self
        defaults.set("positions", forKey: keys.modifierMapping)
        defaults.set(9_000, forKey: keys.pushMilliseconds)
        defaults.set(true, forKey: keys.immediate)
        defaults.set(17, forKey: keys.crossingModifiers)
        let device = try #require(DeviceID(bytes: [UInt8](repeating: 0x42, count: 16)))
        defaults.set(
            [device.hex: 2_000, "not a device": 1_000, String(repeating: "43", count: 16): 50],
            forKey: keys.pointerSpeeds)

        #expect(
            preferences.settings()
                == InputSettings(
                    modifierMapping: .positions, pushMilliseconds: InputSettings.maxPushMilliseconds,
                    immediate: true, crossingModifiers: 17))
        #expect(preferences.pointerSpeeds() == [device: 2_000], "unknown devices and speeds out of range are left out")
    }
}
