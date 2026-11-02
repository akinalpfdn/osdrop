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

    init(
        accessibility: AccessibilityPermission = AccessibilityPermission(),
        cursor: any CursorHiding = BackgroundCursorHider(),
        injector: (any InputInjecting)? = nil,
        focus: any CaptureFocusing = InvisibleKeyWindowFocus(),
        secureInput: any SecureInputChecking = SystemSecureInput(),
        screens: any ScreenDescribing = CGScreenDescriber()
    ) {
        self.accessibility = accessibility
        self.cursor = cursor
        self.capture = EventTapCapture(cursor: cursor)
        self.injector = injector ?? CGEventInjector()
        self.focus = focus
        self.secureInput = secureInput
        self.screens = screens
    }
}
