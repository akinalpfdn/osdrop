# Phase 018 (MVP-18) — Windows app UI
Status: PENDING

DEVPLAN reference: Phase 17. Depends on: Phases 12, 14, 15

## Goal
A simple, clean, finished Windows interface that works the same way as the Mac app.

## Tasks
- [ ] Tray icon whose click opens a panel mirroring the Mac menu (Osplay decision), and a Settings window with the same sections, including the arrangement editor.
- [ ] Pairing dialog, being-controlled indicator, transfer notifications, service and input-process health with repair guidance.
- [ ] P/Invoke layer with one serial queue for engine calls and callback marshaling to the dispatcher.
- [ ] Complete English and Turkish resources, theme dictionaries following the system theme.

## Acceptance Criteria
- [ ] No hardcoded strings or visual values in XAML or code-behind (lint).
- [ ] System light and dark switching is reflected live.
- [ ] A crashed input process or stopped service is surfaced clearly, and the shell never hangs on a native call.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
