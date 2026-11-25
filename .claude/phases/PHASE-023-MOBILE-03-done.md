# Phase 023 (MOBILE-03) — iPhone app
Status: ACTIVE (2026-11-25, user "başlayalım") — after the Android app (user decisions 2026-11-09 and 2026-11-23)

DEVPLAN reference: Phase 22. Depends on: Phase 022

## Goal
An iPhone sends and receives files as the Android app does, while Osdrop is in the foreground on it.

## Tasks
- [x] Swift app with `core` through the Swift package; Keychain identity; Bonjour with the Local Network permission; pairing and the group.
- [x] Received files into the app's folder in the Files app; images and videos to Photos with permission.
- [x] Sending from the app and from a share extension that hands the files to the app.
- [x] Listens only while in the foreground.

## Acceptance Criteria
- [x] An iPhone pairs, appears on both computers, receives files dropped on its corner and sends photos to a computer's box.
- [x] Going to the background ends the session cleanly; reopening reconnects.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09 (user): the app must be open; iOS allows nothing else anyway.
- 2026-11-25: iOS is platform 4 in protocol minor 1; the app is an iOS target of the Mac project; the share extension leaves files in the App Group for the app to send (DECISIONS.md).
