import Testing

@testable import Osdrop

@Suite("Diagnostics report")
struct DiagnosticsReportTests {
    private let report = DiagnosticsReport(
        appVersion: "0.1.0", coreVersion: "1.1.0", system: "macOS 26.0", model: "Mac15,12",
        facts: [("Quality", "high")], logLines: ["core: Connected to Akinalp's PC at 192.168.1.20"],
        privateNames: ["MacBook Air", "Akinalp's PC"])

    @Test("Device names and addresses never reach the report")
    func hidesNamesAndAddresses() {
        let text = report.text
        #expect(!text.contains("Akinalp's PC"))
        #expect(!text.contains("192.168.1.20"))
        #expect(text.contains("<device 2>"))
        #expect(text.contains("<address>"))
        #expect(report.anonymized("fe80::1c2a:3b4c:5d6e:7f80 up") == "<address> up")
    }

    @Test("Versions and settings come first")
    func startsWithVersions() {
        let lines = report.text.split(separator: "\n", omittingEmptySubsequences: false)
        #expect(lines.first == "Osdrop 0.1.0 (core 1.1.0)")
        #expect(lines.contains("Quality: high"))
    }
}
