# Phase 018 (MVP-18) — Windows app UI
Status: BACKLOG — code done (PR #19); the panel, settings and crossing were tried on the PC
(user, 2026-11-09: "çok iyi"); TESTING WU1–WU9 are open as rows

DEVPLAN reference: Phase 17. Depends on: Phases 12, 14, 15

## Goal
A simple, clean, finished Windows interface that works the same way as the Mac app.

## Tasks
- [x] Tray icon whose click opens a panel mirroring the Mac menu (Osplay decision), and a Settings window with the same sections, including the arrangement editor.
- [x] Pairing dialog, being-controlled indicator, transfer notifications, service and input-process health with repair guidance.
- [x] P/Invoke layer with one serial queue for engine calls and callback marshaling to the dispatcher.
- [x] Complete English and Turkish resources, theme dictionaries following the system theme.
- [x] Osdrop's icon on Windows: app icon and tray icons drawn from the Mac menu bar glyph (`scripts/windows/make_icons.py`).
- [x] The PC's chords as settings (user decision, 2026-11-09): recorder, SetChords in the input pipe.
- [x] No crossing while a game holds the cursor (user decision, 2026-11-09), a setting on by default.

## Acceptance Criteria
- [x] No hardcoded strings or visual values in XAML or code-behind (lint).
- [ ] System light and dark switching is reflected live.
- [ ] A crashed input process or stopped service is surfaced clearly, and the shell never hangs on a native call.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09: Osplay's shell pattern; the engine reports the input state; a new name restarts the
  engine; repair runs the service's installer elevated (DECISIONS.md).
- 2026-11-09 (user): the PC's chords are settings, as on the Mac (DECISIONS.md).
- 2026-11-09 (user): no crossing while a game holds the cursor (DECISIONS.md).
