import Foundation

/// Every tunable of the iPhone app in one place. `KeyStore` has the names the Mac's
/// `KeychainKeyStore`, built into this app too, reads.
enum Tunables {
    enum KeyStore {
        static let keychainService = "com.akinalpfdn.osdrop"
        static let identityAccount = "identity"
        static let groupAccount = "group"
        static let itemLabel = "Osdrop"
    }

    enum Mesh {
        /// Items fetched or sent at the same time.
        static let transferStreams: UInt8 = 4
    }

    enum Transfers {
        /// Ended transfers still listed, newest first.
        static let endedRows = 50
        /// Ends of transfers kept until the transfer is listed.
        static let earlyEnds = 64
    }

    enum Handoff {
        /// A set the share extension began but never finished is removed after this long.
        static let unsealedAge: TimeInterval = 60 * 60
    }
}
