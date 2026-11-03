import OsdropCore

/// This device's static key pair (docs/SECURITY_MODEL.md). The app stores `secretKey` in the
/// Keychain; core never persists it.
public struct DeviceIdentity: Sendable {
    public let secretKey: [UInt8]
    public let publicKey: [UInt8]

    /// A new identity from the system's cryptographically secure random generator.
    public static func generate() throws(CoreError) -> DeviceIdentity {
        var source = SystemRandom.source
        var secret = [UInt8](repeating: 0, count: Int(OSDROP_KEY_SIZE))
        var publicKey = [UInt8](repeating: 0, count: Int(OSDROP_KEY_SIZE))
        let result = osdrop_identity_generate(&source, &secret, &publicKey)
        if let error = CoreError(result: result) {
            throw error
        }
        return DeviceIdentity(secretKey: secret, publicKey: publicKey)
    }

    /// Restores an identity from a stored secret key; rejects malformed or all-zero secrets.
    public init(secretKey: [UInt8]) throws(CoreError) {
        guard secretKey.count == Int(OSDROP_KEY_SIZE) else { throw .invalidArgument }
        var publicKey = [UInt8](repeating: 0, count: Int(OSDROP_KEY_SIZE))
        let result = osdrop_identity_public_key(secretKey, &publicKey)
        if let error = CoreError(result: result) {
            throw error
        }
        self.init(secretKey: secretKey, publicKey: publicKey)
    }

    private init(secretKey: [UInt8], publicKey: [UInt8]) {
        self.secretKey = secretKey
        self.publicKey = publicKey
    }

    /// The ID advertised over Bonjour: lowercase hex of the device ID bytes.
    public var deviceID: String { Self.deviceID(publicKey: publicKey) }

    /// Another device's ID, from the public key it presented in the handshake.
    public static func deviceID(publicKey: [UInt8]) -> String {
        guard publicKey.count == Int(OSDROP_KEY_SIZE) else { return "" }
        var id = [UInt8](repeating: 0, count: Int(OSDROP_DEVICE_ID_SIZE))
        // Cannot fail: both buffers have the documented sizes.
        _ = osdrop_identity_device_id(publicKey, &id)
        return id.map { byte in
            let hex = String(byte, radix: 16)
            return byte < 0x10 ? "0" + hex : hex
        }.joined()
    }
}
