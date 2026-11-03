import CoreBridge

enum KeyStoreError: Error, Equatable, Sendable {
    /// The Keychain refused, with this OSStatus.
    case keychain(Int32)
    /// A stored item exists but cannot be read back.
    case damaged
    case identityCreationFailed(CoreError)
}

/// The secrets that decide who may connect: this computer's identity and its group state
/// (docs/SECURITY_MODEL.md, "Stored state").
protocol KeyStoring: Sendable {
    /// The stored identity, or a new one that is stored first.
    func identity() throws(KeyStoreError) -> DeviceIdentity
    /// The bytes core last handed over, or `nil` before the first group change.
    func groupState() throws(KeyStoreError) -> [UInt8]?
    func saveGroupState(_ state: [UInt8]) throws(KeyStoreError)
}
