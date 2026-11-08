import Foundation

/// This Mac's name as other computers show it: its computer name, cut to the 64 bytes a name may
/// have on the wire (docs/PROTOCOL.md "Conventions"), without control characters.
enum DeviceName {
    static let maximumBytes = 64

    static func current() -> String {
        clean(Host.current().localizedName ?? "")
    }

    static func clean(_ name: String) -> String {
        var result = ""
        for character in name
        where !character.unicodeScalars.contains(where: { $0.properties.generalCategory == .control }) {
            guard result.utf8.count + String(character).utf8.count <= maximumBytes else { break }
            result.append(character)
        }
        let trimmed = result.trimmingCharacters(in: .whitespaces)
        return trimmed.isEmpty ? "Mac" : trimmed
    }
}
