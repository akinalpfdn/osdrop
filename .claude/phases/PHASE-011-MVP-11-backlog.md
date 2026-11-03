# Phase 011 (MVP-11) — Discovery, pairing and group on both platforms
Status: BACKLOG — code done (PR #11, draft, CI green); hardware acceptance waits until a second
computer is available (user, 2026-11-03: one Mac only)

DEVPLAN reference: Phase 10. Depends on: Phase 9

## Goal
Macs and PCs find each other, pair into one group and keep the mesh connected.

## Tasks
- [x] Bonjour advertise and browse on macOS (DNS-SD C API) and Windows (DNS-SD APIs).
- [x] Keychain and DPAPI stores for identity, roster and permissions.
- [x] Temporary pairing UI on both platforms (code and confirm); final UI comes in Phases 16 and 17.
- [x] Local Network permission status on macOS; a firewall rule for development builds documented in `BUILDING.md`.
- [x] Debug view of the mesh (members, sessions, RTT).

## Acceptance Criteria
- [ ] Mac ↔ PC, Mac ↔ Mac and PC ↔ PC pair with a code.
- [ ] A third computer pairs with any member and is trusted by all others without another code.
- [ ] A revocation from any member removes the device everywhere.
- [ ] Restarting any app or computer reconnects the mesh without user action.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- The Windows group logic is portable C++ in `windows/shared`, tested on every host.
- Platforms pass every discovery to core and re-register on group changes.
- Temporary pairing UI on both platforms; final UI in Phases 16 and 17.
- Started without the PC (user decision 2026-11-03): Windows is verified by CI; the PC rows of
  the acceptance criteria (G1, G2 PC ↔ PC, and the PC side of G3–G5) wait until the PC is back.
