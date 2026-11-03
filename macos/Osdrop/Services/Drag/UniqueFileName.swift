import Foundation

/// Finder does not resolve name collisions for promised files, so Osdrop picks a free name the way
/// Finder does: "Report.pdf", then "Report 2.pdf", "Report 3.pdf"; folders and files without an
/// extension get the number at the end.
enum UniqueFileName {
    /// The first free name for `desired` in `directory`, given which names exist there.
    static func resolve(_ desired: String, isDirectory: Bool, exists: (String) -> Bool) -> String {
        guard exists(desired) else { return desired }
        let (base, pathExtension) = split(desired, isDirectory: isDirectory)
        var number = 2
        while true {
            let candidate = pathExtension.isEmpty ? "\(base) \(number)" : "\(base) \(number).\(pathExtension)"
            if !exists(candidate) {
                return candidate
            }
            number += 1
        }
    }

    static func resolve(_ desired: String, isDirectory: Bool, in directory: URL) -> String {
        resolve(desired, isDirectory: isDirectory) { name in
            FileManager.default.fileExists(atPath: directory.appendingPathComponent(name).path)
        }
    }

    /// Hidden files (".env") and folders keep their whole name as the base.
    private static func split(_ name: String, isDirectory: Bool) -> (base: String, pathExtension: String) {
        let pathExtension = (name as NSString).pathExtension
        guard !isDirectory, !pathExtension.isEmpty, name != ".\(pathExtension)" else { return (name, "") }
        return ((name as NSString).deletingPathExtension, pathExtension)
    }
}
