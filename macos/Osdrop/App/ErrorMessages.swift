import CoreBridge
import Foundation

/// A localized, actionable message for every error core reports (DEVPLAN Phase 16).
enum ErrorMessages {
    static func text(_ error: CoreError) -> String {
        String(localized: key(error))
    }

    static func key(_ error: CoreError) -> String.LocalizationValue {
        switch error {
        case .unsupportedVersion: "error.unsupportedVersion"
        case .tooLarge: "error.tooLarge"
        case .decryptionFailed, .handshakeOutOfOrder, .replayed, .nonceExhausted, .invalidKey: "error.secure"
        case .timedOut: "error.timedOut"
        case .connectionClosed, .network: "error.network"
        case .peerError, .unexpectedMessage: "error.peer"
        case .notTrusted: "error.notTrusted"
        case .pairingRejected: "error.pairingRejected"
        case .peerBusy: "error.peerBusy"
        case .peerSleeping: "error.peerSleeping"
        case .notPermitted: "error.notPermitted"
        case .groupFull: "error.groupFull"
        case .inAnotherGroup: "error.inAnotherGroup"
        case .invalidLayout: "error.invalidLayout"
        case .cancelled: "error.cancelled"
        case .ioFailure: "error.ioFailure"
        case .noSpace: "error.noSpace"
        case .integrityFailed: "error.integrityFailed"
        case .notFound: "error.notFound"
        case .invalidArgument, .bufferTooSmall, .truncated, .badMagic, .unknownType, .trailingBytes,
            .valueOutOfRange, .invalidString, .randomUnavailable, .invalidState, .internalFailure, .unknown:
            "error.internal"
        }
    }
}
