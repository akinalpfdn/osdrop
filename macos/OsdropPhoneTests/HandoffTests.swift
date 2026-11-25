import Foundation
import Testing

@testable import Osdrop

/// What the share extension hands the app: computers one way, sealed sets of files the other.
@Suite("Share extension handoff")
struct HandoffTests {
    private func handoff() -> Handoff {
        Handoff(root: URL.temporaryDirectory.appending(path: "handoff-\(UUID().uuidString)"))
    }

    @Test("The computers the app saved are what the extension reads")
    func computers() throws {
        let handoff = handoff()
        #expect(handoff.computers().isEmpty)
        let list = [Handoff.Computer(id: "aa", name: "Mac", isWindows: false)]
        try handoff.saveComputers(list)
        #expect(handoff.computers() == list)
    }

    @Test("Only sealed sets are pending, with their files and computer, and go once removed")
    func pendingSends() throws {
        let handoff = handoff()
        let unsealed = try handoff.newSet()
        try Data("x".utf8).write(to: unsealed.appending(path: "draft.txt"))
        let sealed = try handoff.newSet()
        try Data("y".utf8).write(to: sealed.appending(path: "photo.jpg"))
        try handoff.seal(sealed, for: "bb")
        let pending = handoff.pendingSends()
        #expect(pending.count == 1)
        #expect(pending.first?.computer == "bb")
        #expect(pending.first?.files.map(\.lastPathComponent) == ["photo.jpg"])
        handoff.remove(try #require(pending.first))
        #expect(handoff.pendingSends().isEmpty)
    }

    @Test("A set the extension never finished is removed once it is old")
    func unsealedLeftovers() throws {
        let handoff = handoff()
        let unsealed = try handoff.newSet()
        handoff.removeUnsealed(olderThan: 60)
        #expect(FileManager.default.fileExists(atPath: unsealed.path()))
        handoff.removeUnsealed(olderThan: 60, now: .now.addingTimeInterval(120))
        #expect(!FileManager.default.fileExists(atPath: unsealed.path()))
    }
}
