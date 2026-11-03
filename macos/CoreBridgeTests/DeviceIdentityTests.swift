import CoreBridge
import Testing

@Suite("Device identity")
struct DeviceIdentityTests {
    // RFC 7748, section 6.1 (Alice).
    private let aliceSecret: [UInt8] = hexBytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a")
    private let alicePublic: [UInt8] = hexBytes("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")

    @Test("A stored secret restores its public key and device ID")
    func restoresFromSecret() throws {
        let identity = try DeviceIdentity(secretKey: aliceSecret)
        #expect(identity.publicKey == alicePublic)
        #expect(identity.deviceID == "7a91869e4c114964206972403b0a3d81")
    }

    @Test("Generated identities are distinct and restorable")
    func generatesFreshKeys() throws {
        let first = try DeviceIdentity.generate()
        let second = try DeviceIdentity.generate()
        #expect(first.secretKey != second.secretKey)
        #expect(try DeviceIdentity(secretKey: first.secretKey).publicKey == first.publicKey)
    }

    @Test("Malformed secrets are rejected")
    func rejectsMalformedSecrets() {
        #expect(throws: CoreError.invalidKey) { try DeviceIdentity(secretKey: [UInt8](repeating: 0, count: 32)) }
        #expect(throws: CoreError.invalidArgument) { try DeviceIdentity(secretKey: [1, 2, 3]) }
    }
}

private func hexBytes(_ hex: String) -> [UInt8] {
    var bytes: [UInt8] = []
    var index = hex.startIndex
    while index < hex.endIndex {
        let next = hex.index(index, offsetBy: 2)
        bytes.append(UInt8(hex[index..<next], radix: 16) ?? 0)
        index = next
    }
    return bytes
}
