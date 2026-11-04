import CoreBridge
import Foundation

/// The user's input settings (DEVPLAN Phase 12); the settings window comes with Phase 16.
protocol InputPreferencesStoring: Sendable {
    func settings() -> InputSettings
    /// Pointer speed on each computer, in thousandths of this Mac's own.
    func pointerSpeeds() -> [DeviceID: UInt32]
}

/// `@unchecked`: UserDefaults is thread-safe but not marked Sendable.
struct UserDefaultsInputPreferences: InputPreferencesStoring, @unchecked Sendable {
    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func settings() -> InputSettings {
        let keys = Tunables.Settings.Input.self
        var settings = InputSettings()
        if defaults.string(forKey: keys.modifierMapping) == keys.positions {
            settings.modifierMapping = .positions
        }
        if defaults.object(forKey: keys.pushMilliseconds) != nil {
            settings.pushMilliseconds = UInt32(
                clamping: min(
                    max(defaults.integer(forKey: keys.pushMilliseconds), 0), Int(InputSettings.maxPushMilliseconds)))
        }
        settings.immediate = defaults.bool(forKey: keys.immediate)
        settings.crossingModifiers = UInt8(clamping: defaults.integer(forKey: keys.crossingModifiers))
        return settings
    }

    func pointerSpeeds() -> [DeviceID: UInt32] {
        let stored = defaults.dictionary(forKey: Tunables.Settings.Input.pointerSpeeds) ?? [:]
        var speeds: [DeviceID: UInt32] = [:]
        for (hex, value) in stored {
            guard let device = DeviceID(hex: hex), let permille = value as? Int,
                Tunables.Control.pointerSpeeds.contains(permille)
            else { continue }
            speeds[device] = UInt32(permille)
        }
        return speeds
    }
}
