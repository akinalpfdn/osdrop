import CoreBridge

enum DiscoveryError: Error, Equatable, Sendable {
    case invalidAdvertisement(CoreError)
    /// Local Network access is off for Osdrop.
    case localNetworkDenied
    /// DNS-SD failed with this error code.
    case service(Int32)
}

/// One running Bonjour registration.
protocol Advertising: Sendable {
    func cancel()
}

/// A computer found over Bonjour and resolved to an address.
struct DiscoveredDevice: Equatable, Sendable {
    /// The service name: the device ID in hex.
    let label: String
    let advertisement: Advertisement
    let host: String
    let port: UInt16
}

enum DiscoveryEvent: Equatable, Sendable {
    case found(DiscoveredDevice)
    case lost(label: String)
    case failed(DiscoveryError)
}

protocol PeerDiscovering: Sendable {
    /// Announces this computer's listening `port`; the service runs until cancelled.
    func advertise(_ advertisement: Advertisement, port: UInt16) throws(DiscoveryError) -> any Advertising
    /// Every Osdrop computer seen, again whenever its record changes, for as long as the stream is
    /// consumed.
    func browse() -> AsyncStream<DiscoveryEvent>
}
