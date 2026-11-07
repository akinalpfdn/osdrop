import CoreServices
import Foundation

/// The quarantine mark macOS puts on downloads, so Gatekeeper checks a received app or script
/// before it first runs. Only when the user turns it on in Settings (off by default: user decision,
/// Phase 13).
enum FileQuarantine {
    /// Marks `items` and everything inside folders among them; a failure is logged and the rest
    /// is still marked.
    static func mark(_ items: [URL]) {
        var values = URLResourceValues()
        values.quarantineProperties = [
            kLSQuarantineTypeKey as String: kLSQuarantineTypeOtherDownload as String,
            kLSQuarantineAgentNameKey as String: Tunables.Files.quarantineAgent,
        ]
        for item in items {
            mark(item, with: values)
            let contents = FileManager.default.enumerator(at: item, includingPropertiesForKeys: nil)
            while let url = contents?.nextObject() as? URL {
                mark(url, with: values)
            }
        }
    }

    private static func mark(_ url: URL, with values: URLResourceValues) {
        var url = url
        do {
            try url.setResourceValues(values)
        } catch {
            Log.clipboard.error("Marking a received file failed: \(error.localizedDescription, privacy: .public)")
        }
    }
}
