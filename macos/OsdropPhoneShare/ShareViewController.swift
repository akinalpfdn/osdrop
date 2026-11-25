import SwiftUI
import UIKit
import UniformTypeIdentifiers

/// The share sheet's Osdrop (DEVPLAN Phase 22): the user picks a computer, the shared files are
/// copied into the App Group with it, and the app sends them once it is open. An extension cannot
/// keep the network up, and its memory is tight, so it never sends itself.
final class ShareViewController: UIViewController {
    override func viewDidLoad() {
        super.viewDidLoad()
        let handoff = Handoff.shared
        let view = ShareView(computers: handoff?.computers() ?? []) { [weak self] computer in
            await self?.store(for: computer, in: handoff) ?? .failed
        } done: { [weak self] in
            self?.extensionContext?.completeRequest(returningItems: nil)
        }
        let host = UIHostingController(rootView: view)
        addChild(host)
        host.view.frame = self.view.bounds
        host.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        self.view.addSubview(host.view)
        host.didMove(toParent: self)
    }

    /// Copies every shared file into a new set for `computer`.
    private func store(for computer: Handoff.Computer, in handoff: Handoff?) async -> ShareView.Outcome {
        guard let handoff, let folder = try? handoff.newSet() else { return .failed }
        let providers = (extensionContext?.inputItems as? [NSExtensionItem] ?? []).flatMap { $0.attachments ?? [] }
        var copied = 0
        for provider in providers {
            if await Self.copy(provider, into: folder) {
                copied += 1
            }
        }
        guard copied > 0 else {
            try? FileManager.default.removeItem(at: folder)
            return .nothingToSend
        }
        do {
            try handoff.seal(folder, for: computer.id)
        } catch {
            try? FileManager.default.removeItem(at: folder)
            return .failed
        }
        return copied < providers.count ? .someLeftOut : .stored
    }

    /// The provider's file, copied before the system removes it; false for what is not a file.
    private static func copy(_ provider: NSItemProvider, into folder: URL) async -> Bool {
        guard
            let type = provider.registeredContentTypes.first(where: {
                $0.conforms(to: .data) || $0.conforms(to: .package)
            })
        else { return false }
        return await withCheckedContinuation { continuation in
            _ = provider.loadFileRepresentation(for: type, openInPlace: false) { url, _, _ in
                guard let url else {
                    continuation.resume(returning: false)
                    return
                }
                var name = url.lastPathComponent
                if FileManager.default.fileExists(atPath: folder.appending(path: name).path()) {
                    name = "\(UUID().uuidString.prefix(8))-\(name)"
                }
                let copied = (try? FileManager.default.copyItem(at: url, to: folder.appending(path: name))) != nil
                continuation.resume(returning: copied)
            }
        }
    }
}

/// Picks the computer the shared files go to.
struct ShareView: View {
    enum Outcome {
        case stored, someLeftOut, nothingToSend, failed
    }

    let computers: [Handoff.Computer]
    let store: (Handoff.Computer) async -> Outcome
    let done: () -> Void
    @State private var outcome: Outcome?
    @State private var isStoring = false

    var body: some View {
        NavigationStack {
            List {
                if let outcome {
                    Text(Self.message(outcome))
                } else if computers.isEmpty {
                    Text("share.noComputers")
                } else {
                    Section {
                        ForEach(computers) { computer in
                            Button {
                                isStoring = true
                                Task {
                                    outcome = await store(computer)
                                    isStoring = false
                                }
                            } label: {
                                Label(
                                    computer.name, systemImage: computer.isWindows ? Theme.Symbol.pc : Theme.Symbol.mac)
                            }
                            .disabled(isStoring)
                        }
                    } footer: {
                        Text("share.hint")
                    }
                }
            }
            .navigationTitle("share.title")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(outcome == nil ? "share.cancel" : "share.done", action: done)
                        .disabled(isStoring)
                }
            }
        }
    }

    private static func message(_ outcome: Outcome) -> LocalizedStringKey {
        switch outcome {
        case .stored: "share.stored"
        case .someLeftOut: "share.someLeftOut"
        case .nothingToSend: "share.nothingToSend"
        case .failed: "share.failed"
        }
    }
}
