# Phase 017 (MVP-17) — macOS app UI
Status: ACTIVE

DEVPLAN reference: Phase 16. Depends on: Phases 12, 14, 15

## Goal
A simple, clean, finished Mac interface.

## Tasks
- [x] Menu bar panel (transfers in progress wait for Phase 14): group members and their state, which computer has the input, transfers in progress, quick toggles.
- [x] Settings: Arrangement (every member's monitors to scale, drag to place, snapping, live sync), Devices (pair, rename, permissions per device, revoke, leave group), Input (crossing, modifier mapping, pointer speed per computer, hotkeys), Clipboard and Files (toggles, size caps, received-file marking), General (launch at login, update check), Advanced (diagnostics, logs).
- [x] Onboarding (Files and Folders not needed: macOS asks per folder when a copied file is read): Accessibility, Local Network and, if Phase 15 needs it, Files and Folders, with live status and deep links.
- [x] Pairing sheet, being-controlled indicator (Phases 10, 11); transfer notifications wait for Phase 14.
- [x] Complete English and Turkish strings, theme tokens for every visual value, VoiceOver labels; text wraps and never truncates (Osplay lesson).

## Acceptance Criteria
- [x] No hardcoded strings, colors, fonts or spacing in views (lint).
- [ ] Switching system language and appearance updates the UI correctly.
- [x] Every error code maps to a localized, actionable message.
- [ ] An arrangement change on the Mac reaches every member within one second.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-05): this phase now, stacked on `feature/phase-12-fidelity`; transfer
  progress and notifications wait for Phase 14.
- The window, onboarding, menu, update check, launch at login and diagnostics follow Osplay's
  macOS app, so the sibling products look and behave alike.
- Visual direction: a native macOS menu bar utility, system fonts, grouped forms, SF Symbols and
  quiet system colors, as Osplay.
- The return and lock chords became settings, matched in the event tap.
- Renaming this computer restarts the mesh; the arrangement editor snaps computers together
  (DECISIONS.md).
