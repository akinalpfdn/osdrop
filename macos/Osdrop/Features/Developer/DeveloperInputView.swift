#if DEBUG
    import SwiftUI

    struct DeveloperInputView: View {
        static let windowID = "developer"

        @Bindable var viewModel: DeveloperInputViewModel

        var body: some View {
            ScrollView {
                VStack(alignment: .leading, spacing: Theme.Spacing.section) {
                    AccessibilitySection(accessibility: viewModel.accessibility)
                    Divider()
                    TapSection(viewModel: viewModel)
                    Divider()
                    CaptureSection(viewModel: viewModel)
                    Divider()
                    SecureInputSection(status: viewModel.secureInput)
                    Divider()
                    KeyboardSection(viewModel: viewModel)
                    Divider()
                    ScreensSection(screens: viewModel.screens)
                }
                .padding(Theme.Spacing.section)
            }
            .frame(width: Theme.Size.developerWindowWidth)
            .onAppear { viewModel.refresh() }
        }
    }

    private struct SectionTitle: View {
        let key: LocalizedStringKey

        var body: some View {
            Text(key).font(Theme.Typography.title)
        }
    }

    private struct Note: View {
        let text: Text
        var color: Color = Theme.Colors.secondary

        var body: some View {
            text
                .font(Theme.Typography.caption)
                .foregroundStyle(color)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private struct AccessibilitySection: View {
        let accessibility: AccessibilityPermission

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.accessibility.title")
                if accessibility.isTrusted {
                    Label("developer.accessibility.granted", systemImage: Theme.Symbol.granted)
                        .foregroundStyle(Theme.Colors.positive)
                } else {
                    Label("developer.accessibility.missing", systemImage: Theme.Symbol.warning)
                        .foregroundStyle(Theme.Colors.warning)
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: Theme.Spacing.medium) {
                        Button("developer.accessibility.request") { accessibility.requestAccess() }
                        Button("developer.accessibility.openSettings") { accessibility.openSystemSettings() }
                    }
                }
            }
        }
    }

    private struct TapSection: View {
        let viewModel: DeveloperInputViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.tap.title")
                HStack(spacing: Theme.Spacing.medium) {
                    switch viewModel.tapState {
                    case .running:
                        Text("developer.tap.running")
                        Button("developer.tap.stop") { viewModel.stopTap() }
                            .disabled(viewModel.isBusy)
                    case .stopped, .failed:
                        Text("developer.tap.stopped")
                        Button("developer.tap.start") { viewModel.startTap() }
                    }
                }
                if case .failed(let error) = viewModel.tapState {
                    Note(text: Text(error.messageKey), color: Theme.Colors.warning)
                }
                let counts = viewModel.counts
                Note(
                    text: Text(
                        "developer.tap.counts \(counts.pointer) \(counts.buttons) \(counts.scroll) \(counts.keys)"))
                if viewModel.tapReenableCount > 0 {
                    Note(text: Text("developer.tap.reenabled \(viewModel.tapReenableCount)"))
                }
            }
        }
    }

    private struct CaptureSection: View {
        @Bindable var viewModel: DeveloperInputViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.capture.title")
                Note(text: Text("developer.capture.explanation"))
                if viewModel.hidesCursorInBackground {
                    Note(text: Text("developer.cursor.background"))
                } else {
                    Note(text: Text("developer.cursor.foregroundOnly"), color: Theme.Colors.warning)
                }
                HStack(spacing: Theme.Spacing.medium) {
                    Picker("developer.capture.durationLabel", selection: $viewModel.captureSeconds) {
                        ForEach(Tunables.Developer.captureDurations, id: \.self) { seconds in
                            Text("developer.capture.duration \(seconds)").tag(seconds)
                        }
                    }
                    .fixedSize()
                    Button("developer.capture.start") { viewModel.captureAndReplay() }
                        .disabled(!viewModel.canCapture)
                }
                Toggle("developer.capture.replay", isOn: $viewModel.replaysAfterCapture)
                    .disabled(viewModel.isBusy)
                PhaseNote(phase: viewModel.phase)
                if viewModel.focusAcquired == true {
                    Note(text: Text("developer.capture.focusAcquired"))
                } else if viewModel.focusAcquired == false {
                    Note(text: Text("developer.capture.focusRefused"), color: Theme.Colors.warning)
                }
            }
        }
    }

    private struct PhaseNote: View {
        let phase: DeveloperInputViewModel.RunPhase

        var body: some View {
            switch phase {
            case .idle:
                EmptyView()
            case .startingIn(let secondsLeft):
                Note(text: Text("developer.capture.startingIn \(secondsLeft)"), color: Theme.Colors.warning)
            case .capturing(let secondsLeft):
                Note(text: Text("developer.capture.remaining \(secondsLeft)"), color: Theme.Colors.warning)
            case .replaying:
                Note(text: Text("developer.capture.replaying"))
            case .finished(let recorded, let replayed, let release):
                VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                    Note(text: Text("developer.capture.recorded \(recorded)"))
                    if let replayed {
                        Note(text: Text("developer.capture.result \(replayed)"))
                    }
                    switch release {
                    case .requested:
                        EmptyView()
                    case .releaseChord:
                        Note(text: Text("developer.capture.releasedChord"))
                    case .watchdog:
                        Note(text: Text("developer.capture.releasedWatchdog"), color: Theme.Colors.warning)
                    case .tapLost:
                        Note(text: Text("developer.capture.releasedTapLost"), color: Theme.Colors.warning)
                    case .guardUnavailable:
                        Note(text: Text("developer.capture.guardUnavailable"), color: Theme.Colors.warning)
                    }
                }
            }
        }
    }

    private struct SecureInputSection: View {
        let status: SecureInputStatus

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.secureInput.title")
                if !status.isEnabled {
                    Note(text: Text("developer.secureInput.off"))
                } else if let owner = status.ownerName {
                    Note(text: Text("developer.secureInput.on \(owner)"), color: Theme.Colors.warning)
                } else {
                    Note(text: Text("developer.secureInput.onUnknown"), color: Theme.Colors.warning)
                }
            }
        }
    }

    private struct KeyboardSection: View {
        let viewModel: DeveloperInputViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.keyboard.title")
                Note(text: Text("developer.keyboard.layout \(viewModel.layout.name)"))
                if let key = viewModel.lastKey {
                    Text("developer.keyboard.lastKey \(key.hexDescription)")
                        .font(Theme.Typography.monospaced)
                } else {
                    Note(text: Text("developer.keyboard.none"))
                }
                if viewModel.droppedKeyCount > 0 {
                    Note(text: Text("developer.keyboard.dropped \(viewModel.droppedKeyCount)"))
                }
            }
        }
    }

    private struct ScreensSection: View {
        let screens: [ScreenDescription]

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                SectionTitle(key: "developer.screens.title")
                ForEach(screens) { screen in
                    VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                        Text(
                            "developer.screens.row \(screen.sizeText) \(screen.originText) \(screen.scaleText)"
                        )
                        .font(Theme.Typography.monospaced)
                        if screen.isMain {
                            Note(text: Text("developer.screens.main"))
                        }
                    }
                }
            }
        }
    }

    extension CaptureError {
        fileprivate var messageKey: LocalizedStringKey {
            switch self {
            case .tapUnavailable: "developer.tap.unavailable"
            case .threadDidNotStart: "developer.tap.threadFailed"
            case .guardUnavailable: "developer.capture.guardUnavailable"
            }
        }
    }

    extension KeyboardPhysicalLayout {
        /// The layout family's standard abbreviation, the same in every language.
        fileprivate var name: String {
            switch self {
            case .ansi: "ANSI"
            case .iso: "ISO"
            case .jis: "JIS"
            }
        }
    }

    extension ScreenDescription {
        fileprivate var sizeText: String {
            "\(Int(frame.width))×\(Int(frame.height))"
        }

        fileprivate var originText: String {
            "(\(Int(frame.minX)), \(Int(frame.minY)))"
        }

        fileprivate var scaleText: String {
            String(format: "%.2f", scale)
        }
    }
#endif
