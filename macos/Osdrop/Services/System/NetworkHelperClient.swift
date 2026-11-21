import Foundation
import ServiceManagement

/// The network helper's registration with launchd, as SMAppService reports it.
enum NetworkHelperStatus: Sendable, Equatable {
    case notRegistered
    case enabled
    /// Waiting for the user in System Settings › General › Login Items.
    case requiresApproval
    /// The helper is missing from the bundle, or the build cannot run one (ad hoc signing).
    case notFound
}

@MainActor
protocol NetworkHelperRegistering: AnyObject {
    var status: NetworkHelperStatus { get }
    func register() throws
    func unregister()
    func openApproval()
}

@MainActor
protocol NetworkHelperConnecting: AnyObject {
    /// The connection to the helper broke while it was open (the helper ended or crashed).
    var onBroken: (() -> Void)? { get set }
    /// Whether AWDL is off after the request; nil when the helper did not answer.
    func keepAWDLOff(_ off: Bool) async -> Bool?
    func disconnect()
}

/// The helper's launch daemon, from the app's bundle (`Contents/Library/LaunchDaemons`).
@MainActor
final class SystemNetworkHelperRegistration: NetworkHelperRegistering {
    private let service = SMAppService.daemon(plistName: NetworkHelper.plistName)

    var status: NetworkHelperStatus {
        switch service.status {
        case .enabled: .enabled
        case .requiresApproval: .requiresApproval
        case .notRegistered: .notRegistered
        case .notFound: .notFound
        @unknown default: .notFound
        }
    }

    func register() throws {
        try service.register()
    }

    func unregister() {
        do {
            try service.unregister()
        } catch {
            Log.app.error("Unregistering the network helper failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    func openApproval() {
        SMAppService.openSystemSettingsLoginItems()
    }
}

/// The helper over XPC. One connection while the switch is on: closing it, or Osdrop ending,
/// lets AWDL up again.
@MainActor
final class XPCNetworkHelper: NetworkHelperConnecting {
    var onBroken: (() -> Void)?
    private var connection: NSXPCConnection?
    /// Tells the current connection's handlers from those of one closed before.
    private var connectionNumber = 0

    func keepAWDLOff(_ off: Bool) async -> Bool? {
        let connection = connect()
        return await withCheckedContinuation { continuation in
            let once = ResumeOnce(continuation)
            let proxy = connection.remoteObjectProxyWithErrorHandler { error in
                Log.app.error("The network helper did not answer: \(error.localizedDescription, privacy: .public)")
                once.resume(nil)
            }
            guard let helper = proxy as? any NetworkHelperService else {
                once.resume(nil)
                return
            }
            helper.keepAWDLOff(off) { isOff in once.resume(isOff) }
        }
    }

    func disconnect() {
        let closing = connection
        connection = nil
        closing?.invalidate()
    }

    private func connect() -> NSXPCConnection {
        if let connection {
            return connection
        }
        let connection = NSXPCConnection(machServiceName: NetworkHelper.label, options: .privileged)
        connection.remoteObjectInterface = NSXPCInterface(with: (any NetworkHelperService).self)
        connectionNumber += 1
        let number = connectionNumber
        let broken: @Sendable () -> Void = { [weak self] in
            Task { @MainActor in
                // A connection this app closed itself is no news.
                guard let self, self.connectionNumber == number, let connection = self.connection else { return }
                self.connection = nil
                connection.invalidate()
                self.onBroken?()
            }
        }
        connection.interruptionHandler = broken
        connection.invalidationHandler = broken
        connection.resume()
        self.connection = connection
        return connection
    }
}

/// XPC may report both an error and a reply; the caller hears the first.
private final class ResumeOnce: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Bool?, Never>?

    init(_ continuation: CheckedContinuation<Bool?, Never>) {
        self.continuation = continuation
    }

    func resume(_ value: Bool?) {
        let pending = lock.withLock {
            defer { continuation = nil }
            return continuation
        }
        pending?.resume(returning: value)
    }
}
