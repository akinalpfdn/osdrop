import CoreBridge
import SwiftUI

/// Every member's monitors to scale, like System Settings' Arrange view; drag a computer and drop
/// it beside another one. Its monitors move together.
struct ArrangementEditor: View {
    let geometry: ArrangementGeometry
    let move: (DeviceID, CGPoint) -> Void
    @State private var drag: (id: DeviceID, translation: CGSize)?

    var body: some View {
        GeometryReader { proxy in
            let bounds = geometry.bounds
            let scale = bounds.isEmpty ? 1 : min(proxy.size.width / bounds.width, proxy.size.height / bounds.height)
            ZStack(alignment: .topLeading) {
                ForEach(geometry.computers) { computer in
                    let translation = drag?.id == computer.id ? drag?.translation ?? .zero : .zero
                    ComputerBox(computer: computer, scale: scale)
                        .offset(
                            x: (computer.bounds.minX - bounds.minX) * scale + translation.width,
                            y: (computer.bounds.minY - bounds.minY) * scale + translation.height
                        )
                        .gesture(
                            DragGesture()
                                .onChanged { value in drag = (computer.id, value.translation) }
                                .onEnded { value in
                                    drag = nil
                                    move(
                                        computer.id,
                                        CGPoint(
                                            x: computer.offset.x + value.translation.width / scale,
                                            y: computer.offset.y + value.translation.height / scale))
                                }
                        )
                        .accessibilityElement(children: .ignore)
                        .accessibilityLabel(Text(verbatim: computer.name))
                        .accessibilityHint(Text("settings.arrangement.hint"))
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height, alignment: .topLeading)
        }
        .frame(height: Theme.Arrangement.canvasHeight)
    }
}

/// One computer: its monitors where its system puts them, with its name on the largest one.
private struct ComputerBox: View {
    let computer: ArrangementGeometry.Computer
    let scale: CGFloat

    var body: some View {
        let origin = computer.bounds.origin
        ZStack(alignment: .topLeading) {
            ForEach(Array(computer.placedMonitors.enumerated()), id: \.offset) { index, monitor in
                RoundedRectangle(cornerRadius: Theme.Arrangement.cornerRadius)
                    .fill(fill)
                    .overlay(
                        RoundedRectangle(cornerRadius: Theme.Arrangement.cornerRadius)
                            .strokeBorder(Theme.Arrangement.border, lineWidth: Theme.Arrangement.borderWidth)
                    )
                    .overlay {
                        if index == largest {
                            Text(verbatim: computer.name)
                                .font(Theme.Typography.caption)
                                .multilineTextAlignment(.center)
                                .padding(Theme.Spacing.small)
                        }
                    }
                    .frame(width: monitor.width * scale, height: monitor.height * scale)
                    .offset(x: (monitor.minX - origin.x) * scale, y: (monitor.minY - origin.y) * scale)
            }
        }
        .frame(
            width: computer.bounds.width * scale, height: computer.bounds.height * scale, alignment: .topLeading)
    }

    private var fill: Color {
        if computer.isThisMac { return Theme.Arrangement.thisMacFill }
        return computer.isConnected ? Theme.Arrangement.memberFill : Theme.Arrangement.offlineFill
    }

    private var largest: Int {
        computer.placedMonitors.indices.max { lhs, rhs in
            let a = computer.placedMonitors[lhs]
            let b = computer.placedMonitors[rhs]
            return a.width * a.height < b.width * b.height
        } ?? 0
    }
}
