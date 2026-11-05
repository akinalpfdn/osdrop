# Phase 017 (MVP-17) — macOS app UI
Status: ACTIVE

DEVPLAN reference: Phase 16. Depends on: Phases 12, 14, 15

## Goal
A simple, clean, finished Mac interface.

## Tasks
- [ ] Menu bar panel: group members and their state, which computer has the input, transfers in progress, quick toggles.
- [ ] Settings: Arrangement (every member's monitors to scale, drag to place, snapping, live sync), Devices (pair, rename, permissions per device, revoke, leave group), Input (crossing, modifier mapping, pointer speed per computer, hotkeys), Clipboard and Files (toggles, size caps, received-file marking), General (launch at login, update check), Advanced (diagnostics, logs).
- [ ] Onboarding: Accessibility, Local Network and, if Phase 15 needs it, Files and Folders, with live status and deep links.
- [ ] Pairing sheet, being-controlled indicator, transfer notifications.
- [ ] Complete English and Turkish strings, theme tokens for every visual value, VoiceOver labels; text wraps and never truncates (Osplay lesson).

## Acceptance Criteria
- [ ] No hardcoded strings, colors, fonts or spacing in views (lint).
- [ ] Switching system language and appearance updates the UI correctly.
- [ ] Every error code maps to a localized, actionable message.
- [ ] An arrangement change on the Mac reaches every member within one second.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-05): this phase now, stacked on `feature/phase-12-fidelity`; transfer
  progress and notifications wait for Phase 14.
- The window, onboarding, menu, update check, launch at login and diagnostics follow Osplay's
  macOS app, so the sibling products look and behave alike.
