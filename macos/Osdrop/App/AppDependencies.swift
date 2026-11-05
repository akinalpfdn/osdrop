import AppKit
import CoreBridge

/// Composition root: the single place where concrete services are created and injected.
@MainActor
final class AppDependencies {
    let accessibility: AccessibilityPermission
    let cursor: any CursorHiding
    let capture: any InputCapturing
    let injector: any InputInjecting
    let focus: any CaptureFocusing
    let secureInput: any SecureInputChecking
    let screens: any ScreenDescribing
    let staging: DragStaging
    let settings: AppSettings
    let group: GroupController

    init(
        accessibility: AccessibilityPermission = AccessibilityPermission(),
        cursor: any CursorHiding = BackgroundCursorHider(),
        injector: (any InputInjecting)? = nil,
        focus: any CaptureFocusing = InvisibleKeyWindowFocus(),
        secureInput: any SecureInputChecking = SystemSecureInput(),
        screens: any ScreenDescribing = CGScreenDescriber(),
        staging: DragStaging = DragStaging(),
        settings: AppSettings = AppSettings(),
        groupServices: GroupServices? = nil
    ) {
        self.accessibility = accessibility
        self.cursor = cursor
        self.capture = EventTapCapture(cursor: cursor)
        self.injector = injector ?? CGEventInjector()
        self.focus = focus
        self.secureInput = secureInput
        self.screens = screens
        self.staging = staging
        self.settings = settings
        self.group = GroupController(
            services: groupServices ?? .system(settings: settings), screens: screens, settings: settings)
    }
}

extension GroupServices {
    /// The real Keychain, settings, Bonjour and core mesh.
    static func system(settings: AppSettings) -> GroupServices {
        GroupServices(
            keyStore: KeychainKeyStore(), settings: UserDefaultsSettings(), discovery: DNSServiceDiscovery(),
            localNetwork: BonjourLocalNetworkCheck(),
            makeMesh: { configuration throws(CoreError) in try CoreMesh(configuration) },
            deviceName: DeviceName.current(),
            clipboard: ClipboardServices(
                makePasteboard: { GeneralPasteboard() }, staging: ClipboardStaging(),
                converter: BitmapImageConverter()),
            input: .system(preferences: settings))
    }
}

extension InputServices {
    /// An event tap and an injector of input control's own, apart from the developer tools'.
    static func system(preferences: any InputPreferencesStoring) -> InputServices {
        InputServices(
            makeCapture: { EventTapCapture() }, makeInjector: { CGEventInjector() },
            isTrusted: { AXIsProcessTrusted() }, cursorLocation: { CGEventInjector.currentCursorLocation() },
            doubleClickInterval: { NSEvent.doubleClickInterval }, keyboardLayout: { KeyboardPhysicalLayout.current() },
            capsLock: HIDCapsLock(), preferences: preferences)
    }
}
