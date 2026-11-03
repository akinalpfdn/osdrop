# Phase 009 (MVP-09) — Group membership and trust
Status: ACTIVE

DEVPLAN reference: Phase 8. Depends on: Phase 7

## Goal
Computers form one group by pairing with any member; every member trusts every other member, and revocations reach everyone.

## Tasks
- [ ] Roster as a two-phase set keyed by device ID and pairing epoch; roster sync on connect; introductions over authenticated sessions; revocation propagation; leaving a group; the group-size limit.
- [ ] Rules for pairing a device that is already in another group.
- [ ] Local per-device permissions (control, files, clipboard) enforced in `core` with specific refusal codes.
- [ ] Persistence interface for the platforms' secure stores; `capi`.

## Acceptance Criteria
- [ ] Simulated groups converge for every message order, including members that were offline during changes.
- [ ] A revoked device is refused by every member once each has met any up-to-date member; a stale member cannot bring it back.
- [ ] A permission turned off refuses the matching requests with a specific code.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
