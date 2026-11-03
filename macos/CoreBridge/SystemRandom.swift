import OsdropCore
import Security

/// The platform's cryptographically secure random generator, as core expects it.
enum SystemRandom {
    static var source: osdrop_random_source { osdrop_random_source(fill: fill, context: nil) }
}

/// `osdrop_random_source.fill` backed by SecRandomCopyBytes. Thread-safe.
private func fill(
    _ context: UnsafeMutableRawPointer?, _ buffer: UnsafeMutablePointer<UInt8>?, _ length: Int
) -> osdrop_result {
    guard let buffer else { return OSDROP_ERROR_INVALID_ARGUMENT }
    return SecRandomCopyBytes(kSecRandomDefault, length, buffer) == errSecSuccess
        ? OSDROP_OK : OSDROP_ERROR_RANDOM_UNAVAILABLE
}
