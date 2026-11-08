import Foundation

/// Text for bug reports (DEVPLAN Phase 16 Advanced, "Copy diagnostics" in Phase 18): versions, settings, state and the
/// recent log, without device names, keys or network addresses.
struct DiagnosticsReport: Sendable {
    var appVersion: String
    var coreVersion: String
    var system: String
    var model: String
    /// Settings and state as name/value pairs, already free of names.
    var facts: [(String, String)]
    var logLines: [String]
    /// Device names to hide in the log (this Mac's and the paired devices').
    var privateNames: [String]

    var text: String {
        var lines = [
            "Osdrop \(appVersion) (core \(coreVersion))",
            "\(system), \(model)",
            "",
        ]
        lines += facts.map { "\($0.0): \($0.1)" }
        lines += ["", "Log (newest \(logLines.count) lines):"]
        lines += logLines.map(anonymized)
        return lines.joined(separator: "\n")
    }

    /// Replaces device names with placeholders and IP addresses with "<address>".
    func anonymized(_ line: String) -> String {
        var result = line
        for (index, name) in privateNames.enumerated() where !name.isEmpty {
            result = result.replacingOccurrences(of: name, with: "<device \(index + 1)>")
        }
        for pattern in [Self.ipv4, Self.ipv6] {
            result = result.replacingOccurrences(of: pattern, with: "<address>", options: .regularExpression)
        }
        return result
    }

    private static let ipv4 = #"\b\d{1,3}(\.\d{1,3}){3}\b"#
    private static let ipv6 = #"\b[0-9A-Fa-f]{1,4}(:[0-9A-Fa-f]{0,4}){2,7}\b"#

    /// The Mac's model identifier (e.g. "Mac15,12").
    static var hardwareModel: String {
        var size = 0
        sysctlbyname("hw.model", nil, &size, nil, 0)
        var bytes = [CChar](repeating: 0, count: max(size, 1))
        sysctlbyname("hw.model", &bytes, &size, nil, 0)
        return String(decoding: bytes.prefix { $0 != 0 }.map(UInt8.init), as: UTF8.self)
    }
}
