import Foundation

/// The unit tests run inside the app, which is their `TEST_HOST`; Xcode starts it with these
/// variables set. There the app must not act on the user's Mac on its own, such as opening the menu.
enum TestHost {
    private static let variables = ["XCTestSessionIdentifier", "XCTestConfigurationFilePath", "XCTestBundlePath"]

    static let isRunning = variables.contains { ProcessInfo.processInfo.environment[$0] != nil }
}
