# Phase 011 (MVP-11) — Discovery, pairing and group on both platforms
Status: ACTIVE

DEVPLAN reference: Phase 10. Depends on: Phase 9

## Goal
Macs and PCs find each other, pair into one group and keep the mesh connected.

## Tasks
- [ ] Bonjour advertise and browse on macOS (DNS-SD C API) and Windows (DNS-SD APIs).
- [ ] Keychain and DPAPI stores for identity, roster and permissions.
- [ ] Temporary pairing UI on both platforms (code and confirm); final UI comes in Phases 16 and 17.
- [ ] Local Network permission status on macOS; a firewall rule for development builds documented in `BUILDING.md`.
- [ ] Debug view of the mesh (members, sessions, RTT).

## Acceptance Criteria
- [ ] Mac ↔ PC, Mac ↔ Mac and PC ↔ PC pair with a code.
- [ ] A third computer pairs with any member and is trusted by all others without another code.
- [ ] A revocation from any member removes the device everywhere.
- [ ] Restarting any app or computer reconnects the mesh without user action.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
