# Phase 009 (MVP-09) — Group membership and trust
Status: ACTIVE

DEVPLAN reference: Phase 8. Depends on: Phase 7

## Goal
Computers form one group by pairing with any member; every member trusts every other member, and revocations reach everyone.

## Tasks
- [x] Roster as a two-phase set keyed by device ID and pairing epoch; roster sync on connect; introductions over authenticated sessions; revocation propagation; leaving a group; the group-size limit.
- [x] Rules for pairing a device that is already in another group.
- [x] Local per-device permissions (control, files, clipboard) enforced in `core` with specific refusal codes.
- [x] Persistence interface for the platforms' secure stores; `capi`.

## Acceptance Criteria
- [x] Simulated groups converge for every message order, including members that were offline during changes.
- [x] A revoked device is refused by every member once each has met any up-to-date member; a stale member cannot bring it back.
- [x] A permission turned off refuses the matching requests with a specific code.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- The roster keeps one pairing and one revocation per device; a revocation covers every pairing up
  to its epoch.
- Core decides trust; the platforms store the group state as versioned bytes. The C API's trust
  callback, set_group and disconnect are replaced by group functions and callbacks.
- Only members' rosters are merged; a computer joins only from the roster of the computer it just
  paired with.
- Permissions are both announced (Capabilities sent again mid-session) and enforced on receipt.
- At most 8 members and one pairing prompt at a time (new error codes GROUP_FULL 29,
  IN_ANOTHER_GROUP 30).
