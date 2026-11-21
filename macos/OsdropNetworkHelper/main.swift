// Keeps AWDL off while Osdrop asks for it (PHASE-024-NET-01). Runs as root: launchd starts it when
// the app connects (an SMAppService daemon of the app's bundle), and it ends itself once nobody has
// held AWDL off for a while. AWDL takes the Wi-Fi radio off the network's channel at intervals,
// which holds up the other computer's packets; see DECISIONS.md, 2026-11-12.

import Darwin
import Foundation
import OSLog
import Security

private let log = Logger(subsystem: "com.akinalpfdn.osdrop", category: "network-helper")

/// All state lives on one queue: XPC calls, interface changes and the idle timer meet there.
final class AWDLController: @unchecked Sendable {
    /// A mark that AWDL was on before this helper turned it off; /var/run empties at boot.
    private static let markPath = "/var/run/\(NetworkHelper.label).on"
    private static let idleExit = DispatchTimeInterval.seconds(30)

    private let queue = DispatchQueue(label: "\(NetworkHelper.label).awdl")
    private var hold = AWDLHold()
    private var nextClient = 0
    private var routes: DispatchSourceRead?
    private var exitLater: DispatchWorkItem?

    func start() {
        queue.sync {
            let marked = FileManager.default.fileExists(atPath: Self.markPath)
            if let action = hold.started(markedOn: marked) {
                log.info("A previous run held AWDL off; letting it up again")
                perform(action)
            }
            syncMark()
            watchInterfaces()
            scheduleExit()
        }
    }

    func newClient() -> Int {
        queue.sync {
            nextClient += 1
            return nextClient
        }
    }

    /// Whether AWDL is off once the request is applied.
    func keep(_ client: Int, off: Bool) -> Bool {
        queue.sync {
            let action = off ? hold.hold(client, isOn: Self.isOn()) : hold.release(client)
            if let action {
                perform(action)
            }
            syncMark()
            scheduleExit()
            return !Self.isOn()
        }
    }

    private func perform(_ action: AWDLHold.Action) {
        let argument = action == .turnOff ? "down" : "up"
        let process = Process()
        process.executableURL = URL(filePath: "/sbin/ifconfig")
        process.arguments = [NetworkHelper.interface, argument]
        do {
            try process.run()
            process.waitUntilExit()
            log.info("AWDL \(argument, privacy: .public): ifconfig exited with \(process.terminationStatus)")
        } catch {
            log.error("Running ifconfig failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    private func syncMark() {
        if hold.isHolding && hold.restoresOn {
            FileManager.default.createFile(atPath: Self.markPath, contents: Data())
        } else {
            try? FileManager.default.removeItem(atPath: Self.markPath)
        }
    }

    /// The routing socket reports every interface change; AWDL brought back by the system goes
    /// off again at once.
    private func watchInterfaces() {
        let socket = Darwin.socket(PF_ROUTE, SOCK_RAW, 0)
        guard socket >= 0 else {
            log.error("Opening the routing socket failed: \(errno)")
            return
        }
        let source = DispatchSource.makeReadSource(fileDescriptor: socket, queue: queue)
        source.setEventHandler { [self] in
            var message = [UInt8](repeating: 0, count: 2048)
            _ = read(socket, &message, message.count)
            if let action = hold.changed(isOn: Self.isOn()) {
                log.info("The system brought AWDL back")
                perform(action)
                syncMark()
            }
        }
        source.setCancelHandler { close(socket) }
        source.resume()
        routes = source
    }

    private func scheduleExit() {
        exitLater?.cancel()
        guard !hold.isHolding else { return }
        let work = DispatchWorkItem { [self] in
            guard !hold.isHolding else { return }
            log.info("Nobody holds AWDL off; ending")
            exit(0)
        }
        exitLater = work
        queue.asyncAfter(deadline: .now() + Self.idleExit, execute: work)
    }

    private static func isOn() -> Bool {
        var list: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&list) == 0, let first = list else { return false }
        defer { freeifaddrs(list) }
        for entry in sequence(first: first, next: { $0.pointee.ifa_next })
        where String(cString: entry.pointee.ifa_name) == NetworkHelper.interface {
            return (Int32(entry.pointee.ifa_flags) & IFF_UP) != 0
        }
        return false
    }
}

/// One connection from the app.
final class HelperSession: NSObject, NetworkHelperService, @unchecked Sendable {
    private let client: Int
    private let controller: AWDLController

    init(client: Int, controller: AWDLController) {
        self.client = client
        self.controller = controller
    }

    func keepAWDLOff(_ off: Bool, reply: @escaping @Sendable (Bool) -> Void) {
        reply(controller.keep(client, off: off))
    }
}

final class ListenerDelegate: NSObject, NSXPCListenerDelegate, @unchecked Sendable {
    private let controller: AWDLController
    private let requirement: String?

    init(controller: AWDLController, requirement: String?) {
        self.controller = controller
        self.requirement = requirement
    }

    func listener(_ listener: NSXPCListener, shouldAcceptNewConnection connection: NSXPCConnection) -> Bool {
        guard let requirement else {
            log.error("Refused a connection: this helper has no team to check callers against")
            return false
        }
        // Messages from a caller that is not Osdrop's own app are refused and end the connection.
        connection.setCodeSigningRequirement(requirement)
        let client = controller.newClient()
        connection.exportedInterface = NSXPCInterface(with: NetworkHelperService.self)
        connection.exportedObject = HelperSession(client: client, controller: controller)
        let controller = controller
        // The app quit, crashed or let go: AWDL comes back unless another connection holds it.
        connection.invalidationHandler = { _ = controller.keep(client, off: false) }
        connection.interruptionHandler = { _ = controller.keep(client, off: false) }
        connection.resume()
        return true
    }
}

/// The team that signed this helper; the app it serves carries the same.
func signingTeam() -> String? {
    var code: SecCode?
    guard SecCodeCopySelf([], &code) == errSecSuccess, let code else { return nil }
    var staticCode: SecStaticCode?
    guard SecCodeCopyStaticCode(code, [], &staticCode) == errSecSuccess, let staticCode else { return nil }
    var information: CFDictionary?
    let flags = SecCSFlags(rawValue: kSecCSSigningInformation)
    guard SecCodeCopySigningInformation(staticCode, flags, &information) == errSecSuccess,
        let values = information as? [String: Any]
    else { return nil }
    return values[kSecCodeInfoTeamIdentifier as String] as? String
}

let controller = AWDLController()
controller.start()
let delegate = ListenerDelegate(
    controller: controller, requirement: NetworkHelper.clientRequirement(team: signingTeam()))
let listener = NSXPCListener(machServiceName: NetworkHelper.label)
listener.delegate = delegate
listener.resume()
dispatchMain()
