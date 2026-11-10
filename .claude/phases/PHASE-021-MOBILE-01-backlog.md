# Phase 021 (MOBILE-01) — Android receiver
Status: BACKLOG — after 1.0 (user decision 2026-11-09: phones as drop targets, the app open on the phone)

DEVPLAN reference: Phase 20. Depends on: Phase 19

## Goal
An Android phone joins the group and receives files sent to it while Osdrop is open on it.

## Tasks
- [ ] Kotlin app with `core` through JNI; identity in the Android Keystore; NSD discovery; pairing with the six-digit code.
- [ ] Receive-only member: announces file receiving only; no screens, input or clipboard; shown as a phone.
- [ ] Received files into Downloads through MediaStore (images and videos in the gallery), progress, cancel, notification; APKs open the installer.
- [ ] Listens only while the app is open; English and Turkish, light and dark.

## Acceptance Criteria
- [ ] A phone pairs with a Mac and a PC and shows up in both group lists.
- [ ] A 2 GB file and a folder of photos arrive intact over Wi-Fi.
- [ ] Closing the app ends the session; the computers show the phone as not connected.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09 (user): the phone app must be open on the phone; with it closed, the computers show
  no corner target. No background service.
