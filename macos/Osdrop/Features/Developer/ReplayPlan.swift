#if DEBUG
    import CoreGraphics

    /// Turns recorded input back into injector calls: relative movement becomes absolute positions
    /// from the starting point, kept on the displays, with the original timing between events.
    struct ReplayPlan {
        enum Action: Equatable {
            case move(CGPoint)
            case button(MouseButton, isDown: Bool, at: CGPoint)
            case scroll(ScrollDelta)
            case key(HIDKey, KeyAction)
        }

        struct Step: Equatable {
            let delay: Duration
            let action: Action
        }

        let steps: [Step]

        init(recording: [CapturedInput], start: CGPoint, bounds: CGRect, maximumGap: Duration) {
            var position = Self.clamp(start, to: bounds)
            var steps = [Step(delay: .zero, action: .move(position))]
            var previousTimestamp = recording.first?.timestamp
            for input in recording {
                let gap = previousTimestamp.map { input.timestamp >= $0 ? input.timestamp - $0 : 0 } ?? 0
                let delay = min(Duration.nanoseconds(Int64(clamping: gap)), maximumGap)
                previousTimestamp = input.timestamp
                let action: Action
                switch input.event {
                case .pointerMoved(let dx, let dy):
                    position = Self.clamp(CGPoint(x: position.x + dx, y: position.y + dy), to: bounds)
                    action = .move(position)
                case .button(let button, let isDown):
                    action = .button(button, isDown: isDown, at: position)
                case .scroll(let delta):
                    action = .scroll(delta)
                case .key(let key, let keyAction):
                    action = .key(key, keyAction)
                }
                steps.append(Step(delay: delay, action: action))
            }
            self.steps = steps
        }

        /// The last addressable point is one point inside the far edges.
        private static func clamp(_ point: CGPoint, to bounds: CGRect) -> CGPoint {
            guard !bounds.isNull, !bounds.isEmpty else { return point }
            return CGPoint(
                x: min(max(point.x, bounds.minX), bounds.maxX - 1), y: min(max(point.y, bounds.minY), bounds.maxY - 1))
        }
    }
#endif
