# Phase 023 (MOBILE-03) — iPhone receiver
Status: BACKLOG — after 1.0 (user decision 2026-11-09: phones as drop targets, the app open on the phone)

DEVPLAN reference: Phase 22. Depends on: Phase 21

## Goal
An iPhone receives files sent to it, as the Android app does.

## Tasks
- [ ] Swift app with `core` through the Swift package; Keychain identity; Bonjour with the Local Network permission; pairing and the group.
- [ ] Received files into the app's folder in the Files app; images and videos to Photos with permission.
- [ ] Listens only while in the foreground.

## Acceptance Criteria
- [ ] An iPhone pairs, appears on both computers and receives files dropped on its corner.
- [ ] Going to the background ends the session cleanly; reopening reconnects.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09 (user): the phone app must be open on the phone; with it closed, the computers show
  no corner target. No background service.
