import SwiftUI

/// First launch, and later whenever a permission is missing.
struct OnboardingView: View {
    let viewModel: OnboardingViewModel
    let close: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.extraLarge) {
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text("onboarding.title")
                    .font(Theme.Typography.windowTitle)
                Text("onboarding.subtitle")
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            PermissionRow(
                symbol: Theme.Symbol.accessibility, title: "onboarding.accessibility",
                detail: "onboarding.accessibility.detail", granted: viewModel.hasAccessibility,
                action: "onboarding.openSettings", perform: viewModel.allowAccessibility)
            PermissionRow(
                symbol: Theme.Symbol.localNetwork, title: "onboarding.localNetwork",
                detail: "onboarding.localNetwork.detail", granted: viewModel.localNetwork == .allowed,
                action: "onboarding.openSettings", perform: viewModel.openLocalNetworkSettings)
            HStack {
                Spacer()
                Button(viewModel.isComplete ? "onboarding.done" : "onboarding.later") {
                    viewModel.finish()
                    close()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(Theme.Spacing.extraLarge)
        .frame(width: Theme.Size.onboardingWidth)
        .onAppear(perform: viewModel.start)
        .onDisappear(perform: viewModel.stop)
    }
}

private struct PermissionRow: View {
    let symbol: String
    let title: LocalizedStringKey
    let detail: LocalizedStringKey
    let granted: Bool
    let action: LocalizedStringKey
    let perform: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            Image(systemName: symbol)
                .font(Theme.Typography.symbol)
                .frame(width: Theme.Size.symbolColumn)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(title)
                    .font(Theme.Typography.rowTitle)
                Text(detail)
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
            if granted {
                Label("onboarding.granted", systemImage: Theme.Symbol.granted)
                    .labelStyle(.iconOnly)
                    .foregroundStyle(Theme.Colors.positive)
                    .font(Theme.Typography.symbol)
            } else {
                Button(action, action: perform)
            }
        }
        .accessibilityElement(children: .combine)
    }
}
