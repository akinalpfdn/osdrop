import CoreBridge
import PhotosUI
import SwiftUI

/// The app's one screen: pairing with a computer, the group's computers with sending, and the
/// transfers both ways.
struct HomeView: View {
    @Bindable var mesh: PhoneMesh
    /// The computer the picker sends to.
    @State private var target: GroupMember?
    @State private var picksPhotos = false
    @State private var picksFiles = false
    @State private var photos: [PhotosPickerItem] = []
    @State private var asksToLeave = false
    @State private var notice: LocalizedStringKey?

    var body: some View {
        NavigationStack {
            List {
                if mesh.startFailure != nil {
                    Text("home.startFailed")
                        .foregroundStyle(Theme.Colors.warning)
                }
                if mesh.isInGroup {
                    computers
                } else {
                    pairing
                }
                if !mesh.transfers.rows.isEmpty {
                    Section("home.transfers") {
                        ForEach(mesh.transfers.rows) { row in
                            TransferRowView(row: row, peer: name(of: row.peer), notice: $notice) {
                                mesh.cancel(row)
                            }
                        }
                    }
                }
            }
            .navigationTitle("app.name")
            .toolbar {
                if mesh.isInGroup {
                    Menu {
                        Button("home.leaveGroup", role: .destructive) { asksToLeave = true }
                    } label: {
                        Label("home.more", systemImage: Theme.Symbol.more)
                    }
                }
            }
        }
        .photosPicker(
            isPresented: $picksPhotos, selection: $photos, matching: .any(of: [.images, .videos]),
            preferredItemEncoding: .compatible
        )
        .onChange(of: photos) { _, picked in
            guard !picked.isEmpty, let target else { return }
            photos = []
            Task { await sendPhotos(picked, to: target.deviceID) }
        }
        .fileImporter(isPresented: $picksFiles, allowedContentTypes: [.item], allowsMultipleSelection: true) {
            result in
            guard let target, case .success(let urls) = result else { return }
            let staged = urls.compactMap { try? StagedFile(copying: $0) }
            send(staged, picked: urls.count, to: target.deviceID)
        }
        .alert(
            "pairing.title", isPresented: Binding(get: { mesh.pairing != nil }, set: { if !$0 { mesh.pairing = nil } }),
            presenting: mesh.pairing
        ) { _ in
            Button("pairing.accept") { mesh.answerPairing(accepted: true) }
            Button("pairing.reject", role: .cancel) { mesh.answerPairing(accepted: false) }
        } message: { prompt in
            Text("pairing.message \(prompt.name) \(Self.code(prompt.code))")
        }
        .alert(
            "pairing.failed",
            isPresented: Binding(get: { mesh.pairingFailure != nil }, set: { if !$0 { mesh.pairingFailure = nil } })
        ) {
            Button("common.ok") { mesh.pairingFailure = nil }
        } message: {
            Text(PairingFailure.explanation(mesh.pairingFailure))
        }
        .alert(
            notice ?? "", isPresented: Binding(get: { notice != nil }, set: { if !$0 { notice = nil } })
        ) {
            Button("common.ok") { notice = nil }
        }
        .confirmationDialog("home.leaveGroup.title", isPresented: $asksToLeave, titleVisibility: .visible) {
            Button("home.leaveGroup", role: .destructive) { mesh.leaveGroup() }
        } message: {
            Text("home.leaveGroup.message")
        }
    }

    private var pairing: some View {
        Section {
            if mesh.nearby.isEmpty {
                Text("home.pair.searching")
                    .foregroundStyle(Theme.Colors.secondary)
            }
            ForEach(mesh.nearby, id: \.advertisement.deviceID) { device in
                HStack {
                    Label(device.advertisement.name, systemImage: Self.symbol(device.advertisement.platform))
                    Spacer()
                    Button("home.pair.action") { mesh.pair(device) }
                        .buttonStyle(.borderless)
                }
            }
        } header: {
            Text("home.pair.title")
        } footer: {
            Text("home.pair.hint")
        }
    }

    private var computers: some View {
        Section {
            ForEach(mesh.computers) { computer in
                HStack {
                    VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                        Label(computer.name, systemImage: Self.symbol(computer.platform))
                        Text(computer.isConnected ? "home.connected" : "home.notConnected")
                            .font(Theme.Typography.caption)
                            .foregroundStyle(Theme.Colors.secondary)
                    }
                    Spacer()
                    if computer.isConnected {
                        Button {
                            target = computer
                            picksPhotos = true
                        } label: {
                            Label("home.sendPhotos", systemImage: Theme.Symbol.sendPhotos)
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.borderless)
                        Button {
                            target = computer
                            picksFiles = true
                        } label: {
                            Label("home.sendFiles", systemImage: Theme.Symbol.sendFiles)
                                .labelStyle(.iconOnly)
                        }
                        .buttonStyle(.borderless)
                    }
                }
            }
        } header: {
            Text("home.computers")
        } footer: {
            Text("home.computers.hint")
        }
    }

    private func sendPhotos(_ items: [PhotosPickerItem], to computer: DeviceID) async {
        var staged: [StagedFile] = []
        for item in items {
            if let file = try? await item.loadTransferable(type: StagedFile.self) {
                staged.append(file)
            }
        }
        send(staged, picked: items.count, to: computer)
    }

    private func send(_ staged: [StagedFile], picked: Int, to computer: DeviceID) {
        guard !staged.isEmpty, mesh.send(staged, to: computer) else {
            if picked > 0 { notice = "home.notSent" }
            return
        }
        if staged.count < picked {
            notice = "home.someNotRead"
        }
    }

    private func name(of device: DeviceID) -> String {
        mesh.members.first { $0.deviceID == device }?.name ?? ""
    }

    private static func symbol(_ platform: PeerPlatform) -> String {
        platform == .windows ? Theme.Symbol.pc : Theme.Symbol.mac
    }

    /// "123 456", as the computer shows it.
    static func code(_ code: UInt32) -> String {
        let digits = String(format: "%06u", code)
        return "\(digits.prefix(3)) \(digits.suffix(3))"
    }
}

/// What a failed pairing tells the user.
enum PairingFailure {
    static func explanation(_ error: CoreError?) -> LocalizedStringKey {
        switch error {
        case .pairingRejected: "pairing.failed.rejected"
        case .inAnotherGroup: "pairing.failed.anotherGroup"
        case .groupFull: "pairing.failed.groupFull"
        case .timedOut, .network, .connectionClosed: "pairing.failed.network"
        case .unsupportedVersion: "pairing.failed.version"
        default: "pairing.failed.other"
        }
    }
}
