import CoreBridge
import Foundation
import Network
import dnssd

enum LocalNetworkAccess: Equatable, Sendable {
    case unknown
    case allowed
    case denied
}

/// Local Network privacy has no query API; a Bonjour browse reports it: denied browses wait with
/// a policy error, allowed ones become ready. Starting one also makes macOS ask the first time.
protocol LocalNetworkChecking: Sendable {
    /// The current access and every change, for as long as the stream is consumed.
    func accessUpdates() -> AsyncStream<LocalNetworkAccess>
}

struct BonjourLocalNetworkCheck: LocalNetworkChecking {
    func accessUpdates() -> AsyncStream<LocalNetworkAccess> {
        let (stream, continuation) = AsyncStream.makeStream(of: LocalNetworkAccess.self)
        let browser = NWBrowser(for: .bonjour(type: Advertisement.serviceTypeName, domain: nil), using: .tcp)
        browser.stateUpdateHandler = { state in
            switch state {
            case .ready:
                continuation.yield(.allowed)
            case .waiting(let error), .failed(let error):
                continuation.yield(Self.isPolicyDenial(error) ? .denied : .unknown)
            default:
                break
            }
        }
        continuation.onTermination = { _ in browser.cancel() }
        browser.start(queue: DispatchQueue(label: "com.akinalpfdn.osdrop.local-network-check"))
        return stream
    }

    private static func isPolicyDenial(_ error: NWError) -> Bool {
        if case .dns(let code) = error { return code == DNSServiceErrorType(kDNSServiceErr_PolicyDenied) }
        return false
    }
}
