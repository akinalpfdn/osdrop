import CoreBridge
import Foundation
import Observation

/// What input control reads at start (DEVPLAN Phase 12).
@MainActor
protocol InputPreferencesStoring: Sendable {
    var input: InputSettings { get }
    /// Pointer speed on each computer, in thousandths of this Mac's own.
    var pointerSpeeds: [DeviceID: UInt32] { get }
    var hotkeys: CaptureHotkeys { get }
}

/// The user's settings (UserDefaults; the group, layout and listening port are kept by
/// `SettingsStoring`). Every change is saved at once.
@MainActor
@Observable
final class AppSettings: InputPreferencesStoring {
    // MARK: General

    /// Looks for a newer release once a day.
    var checksForUpdates: Bool {
        didSet { defaults.set(checksForUpdates, forKey: Key.checksForUpdates) }
    }

    /// When the last update check got an answer.
    var lastUpdateCheck: Date? {
        didSet { defaults.set(lastUpdateCheck, forKey: Key.lastUpdateCheck) }
    }

    /// The permissions window was finished once; afterwards it opens only when one is missing.
    var hasCompletedOnboarding: Bool {
        didSet { defaults.set(hasCompletedOnboarding, forKey: Key.onboarding) }
    }

    /// The name other computers see; nil: the Mac's own name.
    var deviceName: String? {
        didSet { defaults.set(deviceName, forKey: Key.deviceName) }
    }

    // MARK: Input

    var input: InputSettings {
        didSet { save(input) }
    }

    var pointerSpeeds: [DeviceID: UInt32] {
        didSet {
            defaults.set(
                Dictionary(uniqueKeysWithValues: pointerSpeeds.map { ($0.key.hex, Int($0.value)) }),
                forKey: Key.Input.pointerSpeeds)
        }
    }

    var hotkeys: CaptureHotkeys {
        didSet {
            save(hotkeys.returnHome, forKey: Key.Input.returnHotkey)
            save(hotkeys.lockInput, forKey: Key.Input.lockHotkey)
        }
    }

    // MARK: Clipboard and files

    /// Copies are shared with members whose clipboard permission is on.
    var sharesClipboard: Bool {
        didSet { defaults.set(sharesClipboard, forKey: Key.sharesClipboard) }
    }

    /// Files copied on another computer are fetched up to this total.
    var clipboardFilesLimit: UInt64 {
        didSet { defaults.set(Int(clipboardFilesLimit), forKey: Key.clipboardFilesLimit) }
    }

    /// Received files get the quarantine mark, so Gatekeeper checks them when opened (off by
    /// default: user decision, Phase 13).
    var marksReceivedFiles: Bool {
        didSet { defaults.set(marksReceivedFiles, forKey: Key.marksReceivedFiles) }
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        checksForUpdates = defaults.object(forKey: Key.checksForUpdates) as? Bool ?? true
        lastUpdateCheck = defaults.object(forKey: Key.lastUpdateCheck) as? Date
        hasCompletedOnboarding = defaults.bool(forKey: Key.onboarding)
        deviceName = defaults.string(forKey: Key.deviceName)
        input = Self.loadInput(from: defaults)
        pointerSpeeds = Self.loadSpeeds(from: defaults)
        hotkeys = CaptureHotkeys(
            returnHome: Self.load(Hotkey.self, from: defaults, forKey: Key.Input.returnHotkey)
                ?? Hotkey.defaultReturnHome,
            lockInput: Self.load(Hotkey.self, from: defaults, forKey: Key.Input.lockHotkey)
                ?? Hotkey.defaultLockInput)
        sharesClipboard = defaults.object(forKey: Key.sharesClipboard) as? Bool ?? true
        let limit = defaults.integer(forKey: Key.clipboardFilesLimit)
        clipboardFilesLimit =
            Tunables.Clipboard.filesLimitChoices.contains(UInt64(max(limit, 0)))
            ? UInt64(limit) : Tunables.Clipboard.filesLimit
        marksReceivedFiles = defaults.bool(forKey: Key.marksReceivedFiles)
    }

    private func save(_ input: InputSettings) {
        let keys = Key.Input.self
        defaults.set(
            input.modifierMapping == .positions ? keys.positions : keys.shortcuts, forKey: keys.modifierMapping)
        defaults.set(Int(input.pushMilliseconds), forKey: keys.pushMilliseconds)
        defaults.set(input.immediate, forKey: keys.immediate)
        defaults.set(Int(input.crossingModifiers), forKey: keys.crossingModifiers)
    }

    private func save(_ value: some Encodable, forKey key: String) {
        guard let data = try? JSONEncoder().encode(value) else {  // plain values: never fails
            Log.app.error("Encoding the setting \(key, privacy: .public) failed")
            return
        }
        defaults.set(data, forKey: key)
    }

    private static func load<Value: Decodable>(
        _ type: Value.Type, from defaults: UserDefaults, forKey key: String
    )
        -> Value?
    {
        // A damaged or older value falls back to the default.
        defaults.data(forKey: key).flatMap { try? JSONDecoder().decode(type, from: $0) }
    }

    private static func loadInput(from defaults: UserDefaults) -> InputSettings {
        let keys = Key.Input.self
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

    private static func loadSpeeds(from defaults: UserDefaults) -> [DeviceID: UInt32] {
        let stored = defaults.dictionary(forKey: Key.Input.pointerSpeeds) ?? [:]
        var speeds: [DeviceID: UInt32] = [:]
        for (hex, value) in stored {
            guard let device = DeviceID(hex: hex), let permille = value as? Int,
                Tunables.Control.pointerSpeeds.contains(permille)
            else { continue }
            speeds[device] = UInt32(permille)
        }
        return speeds
    }

    private enum Key {
        static let checksForUpdates = "checkForUpdates"
        static let lastUpdateCheck = "lastUpdateCheck"
        static let onboarding = "completedOnboarding"
        static let deviceName = "deviceName"
        static let sharesClipboard = "shareClipboard"
        static let clipboardFilesLimit = "clipboardFilesLimit"
        static let marksReceivedFiles = "markReceivedFiles"

        enum Input {
            /// "shortcuts" (the default) or "positions".
            static let modifierMapping = "input.modifierMapping"
            static let shortcuts = "shortcuts"
            static let positions = "positions"
            static let pushMilliseconds = "input.pushMilliseconds"
            static let immediate = "input.immediate"
            static let crossingModifiers = "input.crossingModifiers"
            /// Device ID in hex → thousandths of this Mac's speed.
            static let pointerSpeeds = "input.pointerSpeeds"
            static let returnHotkey = "input.returnHotkey"
            static let lockHotkey = "input.lockHotkey"
        }
    }
}
