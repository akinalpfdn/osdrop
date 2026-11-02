import CoreBridge
import Testing

@testable import Osdrop

@Suite("App version")
struct AppVersionTests {
    @Test("Reads the marketing version and build number")
    func readsBothKeys() throws {
        let version = try #require(
            AppVersion(infoDictionary: ["CFBundleShortVersionString": "1.2.3", "CFBundleVersion": "45"]))

        #expect(version.marketing == "1.2.3")
        #expect(version.build == "45")
        #expect(version.description == "1.2.3 (45)")
    }

    @Test(
        "Missing or empty keys give no version",
        arguments: [
            nil,
            [:],
            ["CFBundleShortVersionString": "1.0"],
            ["CFBundleVersion": "1"],
            ["CFBundleShortVersionString": "", "CFBundleVersion": "1"],
            ["CFBundleShortVersionString": "1.0", "CFBundleVersion": ""],
        ] as [[String: String]?])
    func rejectsIncompleteInfo(info: [String: String]?) {
        #expect(AppVersion(infoDictionary: info) == nil)
    }

    @Test("The app bundle carries a version")
    func appBundleHasVersion() {
        #expect(AppVersion.current != nil)
    }

    @Test("The app links the core its headers describe")
    func appLinksMatchingCore() {
        #expect(CoreVersion.linked == CoreVersion.header)
    }
}
