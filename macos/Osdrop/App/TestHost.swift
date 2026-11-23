import Foundation

/// The unit tests run inside the app, which is their `TEST_HOST`; Xcode starts it with these
/// variables set, and `OsdropMain` then starts an empty app instead of Osdrop.
enum TestHost {
    private static let variables = ["XCTestSessionIdentifier", "XCTestConfigurationFilePath", "XCTestBundlePath"]

    static let isRunning = variables.contains { ProcessInfo.processInfo.environment[$0] != nil }
}
