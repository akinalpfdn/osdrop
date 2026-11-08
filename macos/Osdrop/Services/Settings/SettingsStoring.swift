import Foundation

/// Settings that are not secrets: the arrangement core hands over and the listening port, so the
/// Bonjour advertisement stays right across restarts.
protocol SettingsStoring: Sendable {
    func layoutState() -> [UInt8]?
    func saveLayoutState(_ state: [UInt8])
    func listenPort() -> UInt16
    func saveListenPort(_ port: UInt16)
}

/// `@unchecked`: UserDefaults is thread-safe but not marked Sendable.
struct UserDefaultsSettings: SettingsStoring, @unchecked Sendable {
    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func layoutState() -> [UInt8]? {
        defaults.data(forKey: Tunables.Settings.layoutStateKey).map { [UInt8]($0) }
    }

    func saveLayoutState(_ state: [UInt8]) {
        defaults.set(Data(state), forKey: Tunables.Settings.layoutStateKey)
    }

    func listenPort() -> UInt16 {
        UInt16(clamping: defaults.integer(forKey: Tunables.Settings.listenPortKey))
    }

    func saveListenPort(_ port: UInt16) {
        defaults.set(Int(port), forKey: Tunables.Settings.listenPortKey)
    }
}
