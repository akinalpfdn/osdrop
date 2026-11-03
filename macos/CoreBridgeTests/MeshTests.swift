import CoreBridge
import Testing

/// Two meshes on loopback, as the app drives them: they pair with a code, form a group, share
/// their screens and an arrangement, and report it all through the event stream.
@Suite("Mesh through CoreBridge")
struct MeshTests {
    /// Collects events until `condition` holds or 5 s pass.
    private actor Recorder {
        private(set) var events: [MeshEvent] = []

        func record(_ event: MeshEvent) { events.append(event) }

        func wait(for condition: @Sendable ([MeshEvent]) -> Bool) async -> Bool {
            for _ in 0..<500 {
                if condition(events) { return true }
                // A cancelled sleep only ends the wait early; the test checks the condition anyway.
                try? await Task.sleep(for: .milliseconds(10))
            }
            return condition(events)
        }
    }

    private static func listen(to mesh: Mesh) -> Recorder {
        let recorder = Recorder()
        Task { [events = mesh.events] in
            for await event in events {
                await recorder.record(event)
            }
        }
        return recorder
    }

    private static func code(in events: [MeshEvent]) -> (connection: UInt64, code: UInt32)? {
        for event in events {
            if case .pairingCode(let connection, let code, _) = event { return (connection, code) }
        }
        return nil
    }

    private static func monitor(width: Int32, height: Int32) -> ScreenMonitor {
        let frame = CoordRect(x: 0, y: 0, width: width * 16, height: height * 16)
        return ScreenMonitor(
            key: [UInt8](repeating: 7, count: 16), frame: frame, native: frame, pixelWidth: UInt16(width * 2),
            pixelHeight: UInt16(height * 2), isPrimary: true)
    }

    @Test("Two computers pair into a group and share an arrangement")
    func pairsAndArranges() async throws {
        let mac = try Mesh(Mesh.Configuration(identity: try DeviceIdentity.generate(), name: "Mac"))
        let other = try Mesh(Mesh.Configuration(identity: try DeviceIdentity.generate(), name: "Other Mac"))
        let macEvents = Self.listen(to: mac)
        let otherEvents = Self.listen(to: other)
        #expect(mac.listenPort != 0)
        #expect(try mac.groupID() == nil)

        let connection = try other.pair(host: "127.0.0.1", port: mac.listenPort)
        #expect(await macEvents.wait { Self.code(in: $0) != nil })
        #expect(await otherEvents.wait { Self.code(in: $0) != nil })
        let macCode = try #require(Self.code(in: await macEvents.events))
        let otherCode = try #require(Self.code(in: await otherEvents.events))
        #expect(macCode.code == otherCode.code)
        #expect(otherCode.connection == connection)
        try other.confirmPairing(connection, accepted: true)
        try mac.confirmPairing(macCode.connection, accepted: true)

        #expect(
            await macEvents.wait { events in
                events.contains { if case .peerConnected = $0 { true } else { false } }
            })
        #expect(
            await macEvents.wait { events in
                events.contains { if case .groupChanged(_, let id) = $0 { id != nil } else { false } }
            })
        let group = try mac.groupID()
        #expect(group != nil)
        let members = try mac.members()
        #expect(members.count == 2)
        #expect(members.contains { $0.isSelf && $0.deviceID == mac.deviceID })
        #expect(members.contains { !$0.isSelf && $0.isConnected && $0.name == "Other Mac" })

        try mac.setScreens([Self.monitor(width: 1470, height: 956)])
        try other.setScreens([Self.monitor(width: 1512, height: 982)])
        #expect(
            await macEvents.wait { events in
                events.contains { if case .layoutChanged = $0 { true } else { false } }
            })
        var placed = false
        for _ in 0..<200 where !placed {
            placed = try mac.layout().count == 2 && mac.layout().allSatisfy { $0.placement != nil }
            if !placed { try await Task.sleep(for: .milliseconds(10)) }
        }
        #expect(placed)
        #expect(throws: CoreError.invalidArgument) { try mac.setLayout([]) }
    }

    @Test("Commands and stored states are checked")
    func checksArguments() throws {
        #expect(throws: CoreError.badMagic) {
            try Mesh(
                Mesh.Configuration(identity: try DeviceIdentity.generate(), name: "Mac", groupState: [1, 2, 3, 4, 5]))
        }
        #expect(throws: CoreError.invalidString) {
            try Mesh(Mesh.Configuration(identity: try DeviceIdentity.generate(), name: ""))
        }
        let mesh = try Mesh(Mesh.Configuration(identity: try DeviceIdentity.generate(), name: "Mac"))
        #expect(throws: CoreError.invalidState) { try mesh.leaveGroup() }
        #expect(throws: CoreError.invalidArgument) { try mesh.setScreens([]) }
        #expect(try mesh.members().isEmpty)
    }
}
