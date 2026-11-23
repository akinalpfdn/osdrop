# Phase 021 (MOBILE-01) — Android app
Status: PENDING — 0.2.0 (user decisions 2026-11-09 and 2026-11-23)

DEVPLAN reference: Phase 20. Depends on: Phase 19

## Goal
An Android phone joins the group, receives files sent to it and sends files to a computer, while
Osdrop is open on it.

## Tasks
- [ ] `core` for Android: NDK build from the same CMake sources, `OSDROP_PLATFORM_ANDROID`, random bytes from `getrandom`, the computers show the phone as a phone.
- [ ] Kotlin app with `core` through JNI; identity in the Android Keystore; NSD discovery; pairing with the six-digit code; the group roster.
- [ ] Files-only member: announces file transfer only; no screens, input or clipboard.
- [ ] Receiving into Downloads through MediaStore (images and videos in the gallery), progress, cancel, notification; a received APK opens the installer from the notification (`REQUEST_INSTALL_PACKAGES`).
- [ ] Sending from the share sheet and an in-app picker to a chosen connected computer; until Phase 022 the computer saves a phone's files to Downloads with a notification (Mac and PC).
- [ ] Listens only while the app is open; English and Turkish, light and dark; Android 12 or later.
- [ ] Release: APK signed with the shared release keystore, attached to the GitHub release; BUILDING.md and RELEASING.md describe it.

## Acceptance Criteria
- [ ] A phone pairs with a Mac and a PC and shows up in both group lists as a phone.
- [ ] A 2 GB file and a folder of photos arrive intact over Wi-Fi in both directions.
- [ ] An APK sent from a computer installs from the notification.
- [ ] Closing the app ends the session; the computers show the phone as not connected.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09 (user): the phone app must be open on the phone; no background service. Kept 2026-11-23.
- 2026-11-23 (user): files both ways (not receive-only); clipboard later (Phase 025); Android first; GitHub APK first, Google Play later.
