#if DEBUG
    import SwiftUI

    struct DragLoopbackSection: View {
        @Bindable var viewModel: DeveloperDragViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.drag.title").font(Theme.Typography.title)
                DragNote(text: Text("developer.drag.explanation"))
                Toggle("developer.drag.accept", isOn: $viewModel.acceptsDrops)
                HStack(spacing: Theme.Spacing.medium) {
                    if viewModel.isArmed {
                        Button("developer.drag.disarm") { viewModel.disarm() }
                    } else {
                        Button("developer.drag.arm") { viewModel.arm() }
                    }
                    Button("developer.drag.drop") { viewModel.dropWithOsdrop() }
                        .disabled(!viewModel.canDrop)
                }
                DragPhaseNote(viewModel: viewModel)
            }
        }
    }

    private struct DragNote: View {
        let text: Text
        var color: Color = Theme.Colors.secondary

        var body: some View {
            text
                .font(Theme.Typography.caption)
                .foregroundStyle(color)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private struct DragPhaseNote: View {
        let viewModel: DeveloperDragViewModel

        var body: some View {
            switch viewModel.phase {
            case .idle:
                EmptyView()
            case .armed:
                DragNote(text: Text("developer.drag.armed"))
            case .receiving(let count):
                DragNote(text: Text("developer.drag.receiving \(count)"), color: Theme.Colors.warning)
            case .captured(let drag):
                VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                    DragNote(text: Text("developer.drag.captured \(drag.items.count)"))
                    ForEach(drag.items) { item in
                        Text("developer.drag.item \(item.name) \(item.byteCount.formatted(.byteCount(style: .file)))")
                            .font(Theme.Typography.monospaced)
                    }
                    DragNote(text: Text("developer.drag.placeCursor"))
                }
            case .startingIn(let secondsLeft):
                DragNote(text: Text("developer.drag.startingIn \(secondsLeft)"), color: Theme.Colors.warning)
            case .dragging:
                DragNote(text: Text("developer.drag.dragging"))
            case .finished(let outcome):
                switch outcome {
                case .dropped:
                    DragNote(text: Text("developer.drag.dropped \(viewModel.writtenItems) \(viewModel.failedItems)"))
                case .cancelled:
                    DragNote(text: Text("developer.drag.cancelled"), color: Theme.Colors.warning)
                }
            case .failed(let failure):
                switch failure {
                case .captureFailed:
                    DragNote(text: Text("developer.drag.captureFailed"), color: Theme.Colors.warning)
                case .promiseTimedOut:
                    DragNote(text: Text("developer.drag.promiseTimedOut"), color: Theme.Colors.warning)
                case .tapUnavailable:
                    DragNote(text: Text("developer.tap.unavailable"), color: Theme.Colors.warning)
                }
            }
        }
    }
#endif
