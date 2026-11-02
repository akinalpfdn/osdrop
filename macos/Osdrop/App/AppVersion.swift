import Foundation

/// The app's marketing version and build number, read from its Info.plist.
struct AppVersion: Equatable, Sendable {
    let marketing: String
    let build: String

    /// Nil when either key is missing or empty, so a broken bundle is reported rather than shown
    /// as an empty version.
    init?(infoDictionary: [String: Any]?) {
        guard let marketing = infoDictionary?["CFBundleShortVersionString"] as? String, !marketing.isEmpty,
            let build = infoDictionary?["CFBundleVersion"] as? String, !build.isEmpty
        else {
            return nil
        }
        self.marketing = marketing
        self.build = build
    }

    static let current = AppVersion(infoDictionary: Bundle.main.infoDictionary)

    var description: String { "\(marketing) (\(build))" }
}
