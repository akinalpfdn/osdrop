import Foundation

/// Major.minor.patch, comparable with release tags ("v0.2.0").
struct ReleaseVersion: Comparable, Sendable, CustomStringConvertible {
    let major: Int
    let minor: Int
    let patch: Int

    init(major: Int, minor: Int, patch: Int) {
        self.major = major
        self.minor = minor
        self.patch = patch
    }

    /// "v1.2.3", "1.2" or "1.2.3.4" (the fourth part is ignored); nil for anything else.
    init?(_ text: String) {
        let trimmed = text.hasPrefix("v") || text.hasPrefix("V") ? String(text.dropFirst()) : text
        var numbers: [Int] = []
        for part in trimmed.split(separator: ".", omittingEmptySubsequences: false).prefix(3) {
            guard let number = Int(part), number >= 0 else { return nil }
            numbers.append(number)
        }
        guard !numbers.isEmpty else { return nil }
        numbers += Array(repeating: 0, count: 3 - numbers.count)
        self.init(major: numbers[0], minor: numbers[1], patch: numbers[2])
    }

    /// This app's version (CFBundleShortVersionString).
    static var current: ReleaseVersion {
        (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String).flatMap(ReleaseVersion.init)
            ?? ReleaseVersion(major: 0, minor: 0, patch: 0)
    }

    static func < (lhs: ReleaseVersion, rhs: ReleaseVersion) -> Bool {
        (lhs.major, lhs.minor, lhs.patch) < (rhs.major, rhs.minor, rhs.patch)
    }

    var description: String { "\(major).\(minor).\(patch)" }
}

/// A newer release on GitHub.
struct AvailableUpdate: Equatable, Sendable {
    let version: ReleaseVersion
    let page: URL
}

enum UpdateCheckOutcome: Equatable, Sendable {
    case upToDate
    case available(AvailableUpdate)
    case failed
}

/// Finds out whether a newer release exists; `GitHubUpdateChecker` in the app, a fake in tests.
protocol UpdateChecking: Sendable {
    func check(current: ReleaseVersion) async -> UpdateCheckOutcome
}

/// Asks GitHub Releases for the latest version (Osdrop has no server of its own).
struct GitHubUpdateChecker: UpdateChecking {
    func check(current: ReleaseVersion) async -> UpdateCheckOutcome {
        guard let url = Tunables.Updates.latestReleaseURL else { return .failed }
        var request = URLRequest(url: url, timeoutInterval: Tunables.Updates.timeout)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("Osdrop/\(current)", forHTTPHeaderField: "User-Agent")
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            switch (response as? HTTPURLResponse)?.statusCode {
            case 200: return Self.outcome(from: data, current: current)
            case 404: return .upToDate  // nothing released yet
            default: return .failed
            }
        } catch {
            Log.app.warning("The update check failed: \(String(describing: error), privacy: .public)")
            return .failed
        }
    }

    /// The answer of GitHub's "latest release" endpoint compared with this version.
    static func outcome(from data: Data, current: ReleaseVersion) -> UpdateCheckOutcome {
        struct Release: Decodable {
            let tagName: String
            let htmlURL: URL

            enum CodingKeys: String, CodingKey {
                case tagName = "tag_name"
                case htmlURL = "html_url"
            }
        }
        guard let release = try? JSONDecoder().decode(Release.self, from: data),
            let latest = ReleaseVersion(release.tagName)
        else { return .failed }
        return latest > current ? .available(AvailableUpdate(version: latest, page: release.htmlURL)) : .upToDate
    }
}
