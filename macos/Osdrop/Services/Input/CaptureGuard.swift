import Darwin
import Foundation

/// The link to the process that ends Osdrop if it hangs while capturing input.
protocol CaptureGuarding: Sendable {
    /// Starts the guard if it is not running. False when it cannot run; capture must not start then.
    func ensureRunning() -> Bool
    func send(_ message: CaptureGuardMessage)
}

/// Runs `OsdropCaptureGuard` from the app bundle and talks to it through its standard input.
final class ProcessCaptureGuard: CaptureGuarding {
    private struct Running {
        let process: Process
        let input: FileHandle
    }

    private let executable: URL?
    private let target: pid_t
    private let running = Locked<Running?>(nil)

    init(
        executable: URL? = Bundle.main.url(forAuxiliaryExecutable: CaptureGuardProtocol.executableName),
        target: pid_t = getpid()
    ) {
        self.executable = executable
        self.target = target
    }

    deinit {
        running.withLock { state in
            state?.process.terminate()
            state = nil
        }
    }

    func ensureRunning() -> Bool {
        running.withLock { state in
            if let current = state, current.process.isRunning {
                return true
            }
            state = start()
            return state != nil
        }
    }

    func send(_ message: CaptureGuardMessage) {
        let descriptor = running.withLock { $0?.input.fileDescriptor }
        guard let descriptor else { return }
        var byte = message.rawValue
        if write(descriptor, &byte, 1) != 1 {
            Log.input.error("Writing to the capture guard failed: \(errno, privacy: .public)")
        }
    }

    private func start() -> Running? {
        guard let executable else {
            Log.input.error("The capture guard is missing from the app bundle")
            return nil
        }
        let process = Process()
        let pipe = Pipe()
        process.executableURL = executable
        process.arguments = [String(target)]
        process.standardInput = pipe
        // The guard must keep its timing even when the system is busy.
        process.qualityOfService = .userInteractive
        do {
            try process.run()
        } catch {
            Log.input.error("Starting the capture guard failed: \(error.localizedDescription, privacy: .public)")
            return nil
        }
        // A guard that died must not take Osdrop down with SIGPIPE on the next write.
        _ = fcntl(pipe.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)
        return Running(process: process, input: pipe.fileHandleForWriting)
    }
}
