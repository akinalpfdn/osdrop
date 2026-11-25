import Photos
import UniformTypeIdentifiers

/// Saves received images and videos to Photos, with the user's permission to add.
enum PhotoSaver {
    enum Failure: Error {
        case notAllowed
        case failed
    }

    /// Whether a received file can go to Photos.
    static func canSave(_ file: URL) -> Bool {
        guard let type = UTType(filenameExtension: file.pathExtension) else { return false }
        return type.conforms(to: .image) || type.conforms(to: .movie)
    }

    static func save(_ file: URL) async throws(Failure) {
        let status = await PHPhotoLibrary.requestAuthorization(for: .addOnly)
        guard status == .authorized || status == .limited else { throw .notAllowed }
        let isVideo = UTType(filenameExtension: file.pathExtension)?.conforms(to: .movie) ?? false
        do {
            try await PHPhotoLibrary.shared().performChanges {
                if isVideo {
                    PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: file)
                } else {
                    PHAssetChangeRequest.creationRequestForAssetFromImage(atFileURL: file)
                }
            }
        } catch {
            throw .failed
        }
    }
}
