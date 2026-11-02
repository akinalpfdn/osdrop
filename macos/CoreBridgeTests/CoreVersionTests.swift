import CoreBridge
import Testing

@Suite("Core version")
struct CoreVersionTests {
    @Test("The linked core matches the header the bridge was built against")
    func linkedVersionMatchesHeader() {
        #expect(CoreVersion.linked == CoreVersion.header)
    }

    @Test("Version renders as a dotted triple")
    func description() {
        #expect(CoreVersion(major: 1, minor: 2, patch: 3).description == "1.2.3")
    }
}
