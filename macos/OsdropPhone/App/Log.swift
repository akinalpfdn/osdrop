import os

/// Unified logging for the iPhone app; file contents and keys are never logged. The Mac's
/// discovery and key store sources, built into this app too, log through `discovery`.
enum Log {
    static let subsystem = "com.akinalpfdn.osdrop.ios"

    static let app = Logger(subsystem: subsystem, category: "app")
    static let discovery = Logger(subsystem: subsystem, category: "discovery")
    static let mesh = Logger(subsystem: subsystem, category: "mesh")
    static let files = Logger(subsystem: subsystem, category: "files")
}
