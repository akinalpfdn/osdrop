import CoreBridge
import Foundation
import OSLog
import dnssd

/// Bonjour through the system DNS-SD API. Network.framework cannot advertise a port its own
/// listener does not own (core owns the socket), and resolves services only by connecting to them.
struct DNSServiceDiscovery: PeerDiscovering {
    func advertise(_ advertisement: Advertisement, port: UInt16) throws(DiscoveryError) -> any Advertising {
        let record: [String: String]
        do throws(CoreError) {
            record = try advertisement.txtRecord()
        } catch {
            throw .invalidAdvertisement(error)
        }
        return try DNSServiceRegistration(name: advertisement.deviceID.hex, port: port, txt: record)
    }

    func browse() -> AsyncStream<DiscoveryEvent> {
        let (stream, continuation) = AsyncStream.makeStream(of: DiscoveryEvent.self)
        let browser = DNSServiceBrowser(continuation: continuation)
        continuation.onTermination = { _ in browser.stop() }
        browser.start()
        return stream
    }
}

/// One registration. `@unchecked`: `ref` is written once in init and released on `queue`.
private final class DNSServiceRegistration: Advertising, @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.akinalpfdn.osdrop.bonjour.register")
    private var ref: DNSServiceRef?

    init(name: String, port: UInt16, txt: [String: String]) throws(DiscoveryError) {
        var record = TXTRecordRef()
        TXTRecordCreate(&record, 0, nil)
        defer { TXTRecordDeallocate(&record) }
        for (key, value) in txt.sorted(by: { $0.key < $1.key }) {
            let bytes = Array(value.utf8)
            TXTRecordSetValue(&record, key, UInt8(bytes.count), bytes)
        }
        var created: DNSServiceRef?
        let error = DNSServiceRegister(
            &created, 0, 0, name, Advertisement.serviceType, nil, nil, port.bigEndian,
            TXTRecordGetLength(&record), TXTRecordGetBytesPtr(&record), nil, nil)
        guard error == kDNSServiceErr_NoError, let created else { throw .service(error) }
        DNSServiceSetDispatchQueue(created, queue)
        ref = created
    }

    func cancel() {
        queue.sync {
            if let ref { DNSServiceRefDeallocate(ref) }
            ref = nil
        }
    }

    deinit {
        if let ref { DNSServiceRefDeallocate(ref) }
    }
}

/// Browses and resolves on one serial queue. `@unchecked`: all mutable state is touched only on
/// `queue` (DNS-SD delivers callbacks there).
private final class DNSServiceBrowser: @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.akinalpfdn.osdrop.bonjour.browse")
    private let continuation: AsyncStream<DiscoveryEvent>.Continuation
    private var browseRef: DNSServiceRef?
    private var resolving: [String: DNSServiceRef] = [:]
    private var found: Set<String> = []
    private var stopped = false

    init(continuation: AsyncStream<DiscoveryEvent>.Continuation) {
        self.continuation = continuation
    }

    func start() {
        queue.async { [self] in
            var ref: DNSServiceRef?
            let context = Unmanaged.passUnretained(self).toOpaque()
            let error = DNSServiceBrowse(
                &ref, 0, 0, Advertisement.serviceType, nil,
                { _, flags, interface, error, name, _, domain, context in
                    guard let context else { return }
                    let browser = Unmanaged<DNSServiceBrowser>.fromOpaque(context).takeUnretainedValue()
                    guard error == kDNSServiceErr_NoError, let name, let domain else {
                        browser.failed(error)
                        return
                    }
                    browser.browsed(
                        name: String(cString: name), domain: String(cString: domain), interface: interface,
                        added: flags & kDNSServiceFlagsAdd != 0)
                }, context)
            guard error == kDNSServiceErr_NoError, let ref else {
                failed(error)
                return
            }
            DNSServiceSetDispatchQueue(ref, queue)
            browseRef = ref
            // Keeps `self` alive while DNS-SD holds its pointer; released in stop().
            _ = Unmanaged.passRetained(self)
        }
    }

    func stop() {
        queue.async { [self] in
            guard !stopped else { return }
            stopped = true
            resolving.values.forEach { DNSServiceRefDeallocate($0) }
            resolving = [:]
            if let browseRef {
                DNSServiceRefDeallocate(browseRef)
                self.browseRef = nil
                Unmanaged.passUnretained(self).release()
            }
        }
    }

    private func failed(_ error: DNSServiceErrorType) {
        guard !stopped else { return }
        Log.discovery.error("Bonjour browse failed: \(error)")
        let reason: DiscoveryError =
            error == kDNSServiceErr_PolicyDenied ? .localNetworkDenied : .service(error)
        continuation.yield(.failed(reason))
        continuation.finish()
    }

    private func browsed(name: String, domain: String, interface: UInt32, added: Bool) {
        guard !stopped else { return }
        guard added else {
            if found.remove(name) != nil {
                continuation.yield(.lost(label: name))
            }
            return
        }
        guard resolving[name] == nil else { return }
        var ref: DNSServiceRef?
        let context = Unmanaged.passUnretained(self).toOpaque()
        let error = DNSServiceResolve(
            &ref, 0, interface, name, Advertisement.serviceType, domain,
            { _, _, _, error, fullName, host, port, length, txt, context in
                guard let context, let fullName else { return }
                let browser = Unmanaged<DNSServiceBrowser>.fromOpaque(context).takeUnretainedValue()
                let record = error == kDNSServiceErr_NoError ? DNSServiceBrowser.parse(length: length, txt: txt) : nil
                browser.resolved(
                    fullName: String(cString: fullName), host: host.map { String(cString: $0) },
                    port: UInt16(bigEndian: port), record: record)
            }, context)
        guard error == kDNSServiceErr_NoError, let ref else { return }
        DNSServiceSetDispatchQueue(ref, queue)
        resolving[name] = ref
    }

    private func resolved(fullName: String, host: String?, port: UInt16, record: [String: String]?) {
        // The full name is "<label>._osdrop._tcp.local."; labels are hex device IDs, never escaped.
        guard !stopped, let label = fullName.split(separator: ".").first.map(String.init) else { return }
        if let ref = resolving.removeValue(forKey: label) {
            DNSServiceRefDeallocate(ref)
        }
        guard let host, let record else { return }
        do throws(CoreError) {
            let advertisement = try Advertisement(txtRecord: record)
            found.insert(label)
            let device = DiscoveredDevice(label: label, advertisement: advertisement, host: host, port: port)
            continuation.yield(.found(device))
        } catch {
            Log.discovery.info("Ignoring \(label, privacy: .public): \(String(describing: error), privacy: .public)")
        }
    }

    private static func parse(length: UInt16, txt: UnsafePointer<UInt8>?) -> [String: String] {
        var record: [String: String] = [:]
        let count = TXTRecordGetCount(length, txt)
        var key = [CChar](repeating: 0, count: 256)
        for index in 0..<count {
            var valueLength: UInt8 = 0
            var value: UnsafeRawPointer?
            guard
                TXTRecordGetItemAtIndex(length, txt, index, UInt16(key.count), &key, &valueLength, &value)
                    == kDNSServiceErr_NoError
            else { continue }
            let name = String(decoding: key.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
            let bytes = value.map { Array(UnsafeRawBufferPointer(start: $0, count: Int(valueLength))) } ?? []
            record[name] = String(decoding: bytes, as: UTF8.self)
        }
        return record
    }
}
