import OsdropCore

/// A failure reported by the shared core (`osdrop_result`). The UI maps cases to localized text.
public enum CoreError: Error, Equatable, Sendable {
    case invalidArgument
    case bufferTooSmall
    case truncated
    case badMagic
    case unsupportedVersion
    case unknownType
    case trailingBytes
    case valueOutOfRange
    case invalidString
    case tooLarge
    case decryptionFailed
    case handshakeOutOfOrder
    case replayed
    case nonceExhausted
    case invalidKey
    case randomUnavailable
    case timedOut
    case connectionClosed
    case peerError
    case unexpectedMessage
    case notTrusted
    case pairingRejected
    case peerBusy
    case invalidState
    case network
    case internalFailure
    /// The peer closed because it is going to sleep; it is expected back.
    case peerSleeping
    /// This computer's local permissions refuse the request from this peer.
    case notPermitted
    /// The group already has the most computers allowed.
    case groupFull
    /// The peer belongs to another group; one of them must leave it first.
    case inAnotherGroup
    /// The arrangement has overlapping monitors, or a computer that touches no other.
    case invalidLayout
    /// A transfer was cancelled on either side.
    case cancelled
    /// A file could not be read or written.
    case ioFailure
    /// Not enough free space for a transfer.
    case noSpace
    /// Received content does not match the sender's digest.
    case integrityFailed
    /// The offered item or file no longer exists.
    case notFound
    /// A code added to the core after this app was built.
    case unknown(code: UInt32)

    /// `nil` for `OSDROP_OK`.
    public init?(result: osdrop_result) {
        switch result {
        case OSDROP_OK: return nil
        case OSDROP_ERROR_INVALID_ARGUMENT: self = .invalidArgument
        case OSDROP_ERROR_BUFFER_TOO_SMALL: self = .bufferTooSmall
        case OSDROP_ERROR_TRUNCATED: self = .truncated
        case OSDROP_ERROR_BAD_MAGIC: self = .badMagic
        case OSDROP_ERROR_UNSUPPORTED_VERSION: self = .unsupportedVersion
        case OSDROP_ERROR_UNKNOWN_TYPE: self = .unknownType
        case OSDROP_ERROR_TRAILING_BYTES: self = .trailingBytes
        case OSDROP_ERROR_VALUE_OUT_OF_RANGE: self = .valueOutOfRange
        case OSDROP_ERROR_INVALID_STRING: self = .invalidString
        case OSDROP_ERROR_TOO_LARGE: self = .tooLarge
        case OSDROP_ERROR_DECRYPTION_FAILED: self = .decryptionFailed
        case OSDROP_ERROR_HANDSHAKE_OUT_OF_ORDER: self = .handshakeOutOfOrder
        case OSDROP_ERROR_REPLAYED: self = .replayed
        case OSDROP_ERROR_NONCE_EXHAUSTED: self = .nonceExhausted
        case OSDROP_ERROR_INVALID_KEY: self = .invalidKey
        case OSDROP_ERROR_RANDOM_UNAVAILABLE: self = .randomUnavailable
        case OSDROP_ERROR_TIMED_OUT: self = .timedOut
        case OSDROP_ERROR_CONNECTION_CLOSED: self = .connectionClosed
        case OSDROP_ERROR_PEER_ERROR: self = .peerError
        case OSDROP_ERROR_UNEXPECTED_MESSAGE: self = .unexpectedMessage
        case OSDROP_ERROR_NOT_TRUSTED: self = .notTrusted
        case OSDROP_ERROR_PAIRING_REJECTED: self = .pairingRejected
        case OSDROP_ERROR_PEER_BUSY: self = .peerBusy
        case OSDROP_ERROR_INVALID_STATE: self = .invalidState
        case OSDROP_ERROR_NETWORK: self = .network
        case OSDROP_ERROR_INTERNAL: self = .internalFailure
        case OSDROP_ERROR_PEER_SLEEPING: self = .peerSleeping
        case OSDROP_ERROR_NOT_PERMITTED: self = .notPermitted
        case OSDROP_ERROR_GROUP_FULL: self = .groupFull
        case OSDROP_ERROR_IN_ANOTHER_GROUP: self = .inAnotherGroup
        case OSDROP_ERROR_INVALID_LAYOUT: self = .invalidLayout
        case OSDROP_ERROR_CANCELLED: self = .cancelled
        case OSDROP_ERROR_IO: self = .ioFailure
        case OSDROP_ERROR_NO_SPACE: self = .noSpace
        case OSDROP_ERROR_INTEGRITY_FAILED: self = .integrityFailed
        case OSDROP_ERROR_NOT_FOUND: self = .notFound
        default: self = .unknown(code: result.rawValue)
        }
    }

    /// The numeric `osdrop_result` value.
    public var code: UInt32 {
        switch self {
        case .invalidArgument: OSDROP_ERROR_INVALID_ARGUMENT.rawValue
        case .bufferTooSmall: OSDROP_ERROR_BUFFER_TOO_SMALL.rawValue
        case .truncated: OSDROP_ERROR_TRUNCATED.rawValue
        case .badMagic: OSDROP_ERROR_BAD_MAGIC.rawValue
        case .unsupportedVersion: OSDROP_ERROR_UNSUPPORTED_VERSION.rawValue
        case .unknownType: OSDROP_ERROR_UNKNOWN_TYPE.rawValue
        case .trailingBytes: OSDROP_ERROR_TRAILING_BYTES.rawValue
        case .valueOutOfRange: OSDROP_ERROR_VALUE_OUT_OF_RANGE.rawValue
        case .invalidString: OSDROP_ERROR_INVALID_STRING.rawValue
        case .tooLarge: OSDROP_ERROR_TOO_LARGE.rawValue
        case .decryptionFailed: OSDROP_ERROR_DECRYPTION_FAILED.rawValue
        case .handshakeOutOfOrder: OSDROP_ERROR_HANDSHAKE_OUT_OF_ORDER.rawValue
        case .replayed: OSDROP_ERROR_REPLAYED.rawValue
        case .nonceExhausted: OSDROP_ERROR_NONCE_EXHAUSTED.rawValue
        case .invalidKey: OSDROP_ERROR_INVALID_KEY.rawValue
        case .randomUnavailable: OSDROP_ERROR_RANDOM_UNAVAILABLE.rawValue
        case .timedOut: OSDROP_ERROR_TIMED_OUT.rawValue
        case .connectionClosed: OSDROP_ERROR_CONNECTION_CLOSED.rawValue
        case .peerError: OSDROP_ERROR_PEER_ERROR.rawValue
        case .unexpectedMessage: OSDROP_ERROR_UNEXPECTED_MESSAGE.rawValue
        case .notTrusted: OSDROP_ERROR_NOT_TRUSTED.rawValue
        case .pairingRejected: OSDROP_ERROR_PAIRING_REJECTED.rawValue
        case .peerBusy: OSDROP_ERROR_PEER_BUSY.rawValue
        case .invalidState: OSDROP_ERROR_INVALID_STATE.rawValue
        case .network: OSDROP_ERROR_NETWORK.rawValue
        case .internalFailure: OSDROP_ERROR_INTERNAL.rawValue
        case .peerSleeping: OSDROP_ERROR_PEER_SLEEPING.rawValue
        case .notPermitted: OSDROP_ERROR_NOT_PERMITTED.rawValue
        case .groupFull: OSDROP_ERROR_GROUP_FULL.rawValue
        case .inAnotherGroup: OSDROP_ERROR_IN_ANOTHER_GROUP.rawValue
        case .invalidLayout: OSDROP_ERROR_INVALID_LAYOUT.rawValue
        case .cancelled: OSDROP_ERROR_CANCELLED.rawValue
        case .ioFailure: OSDROP_ERROR_IO.rawValue
        case .noSpace: OSDROP_ERROR_NO_SPACE.rawValue
        case .integrityFailed: OSDROP_ERROR_INTEGRITY_FAILED.rawValue
        case .notFound: OSDROP_ERROR_NOT_FOUND.rawValue
        case .unknown(let code): code
        }
    }
}
