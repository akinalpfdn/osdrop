import CoreBridge
import Foundation
import Security

/// Generic-password items under the app's identifier: the 32-byte secret key and the group state.
/// Readable only on this Mac and only after its first unlock.
struct KeychainKeyStore: KeyStoring {
    private let service: String

    init(service: String = Tunables.KeyStore.keychainService) {
        self.service = service
    }

    func identity() throws(KeyStoreError) -> DeviceIdentity {
        if let secret = try read(account: Tunables.KeyStore.identityAccount) {
            do throws(CoreError) {
                return try DeviceIdentity(secretKey: [UInt8](secret))
            } catch {
                throw .damaged
            }
        }
        let identity: DeviceIdentity
        do throws(CoreError) {
            identity = try DeviceIdentity.generate()
        } catch {
            throw .identityCreationFailed(error)
        }
        try write(Data(identity.secretKey), account: Tunables.KeyStore.identityAccount)
        return identity
    }

    func groupState() throws(KeyStoreError) -> [UInt8]? {
        try read(account: Tunables.KeyStore.groupAccount).map { [UInt8]($0) }
    }

    func saveGroupState(_ state: [UInt8]) throws(KeyStoreError) {
        try write(Data(state), account: Tunables.KeyStore.groupAccount)
    }

    private func baseQuery(account: String) -> [CFString: Any] {
        [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: account]
    }

    private func read(account: String) throws(KeyStoreError) -> Data? {
        var query = baseQuery(account: account)
        query[kSecReturnData] = true
        query[kSecMatchLimit] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        switch status {
        case errSecSuccess:
            guard let data = result as? Data else { throw .damaged }
            return data
        case errSecItemNotFound:
            return nil
        default:
            throw .keychain(status)
        }
    }

    private func write(_ data: Data, account: String) throws(KeyStoreError) {
        let query = baseQuery(account: account)
        let update: [CFString: Any] = [kSecValueData: data]
        var status = SecItemUpdate(query as CFDictionary, update as CFDictionary)
        if status == errSecItemNotFound {
            var item = query
            item[kSecValueData] = data
            item[kSecAttrAccessible] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            item[kSecAttrLabel] = Tunables.KeyStore.itemLabel
            status = SecItemAdd(item as CFDictionary, nil)
        }
        guard status == errSecSuccess else { throw .keychain(status) }
    }
}
