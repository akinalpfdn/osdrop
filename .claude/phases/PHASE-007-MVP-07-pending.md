# Phase 007 (MVP-07) — Crypto and handshake
Status: PENDING

DEVPLAN reference: Phase 6. Depends on: Phase 5

## Goal
Two `core` instances authenticate each other, show the same pairing code and derive keys for control, input datagrams and transfer streams.

## Tasks
- [ ] Monocypher vendored with its license.
- [ ] Noise XX, cipher states, replay window and platform-supplied randomness copied from Osplay at a recorded commit, with an Osdrop prologue and labels; key derivation for input datagrams and per-stream transfer keys; pairing code.
- [ ] `capi` for identity generation, import and export (storage stays on the platforms).

## Acceptance Criteria
- [ ] Published Noise XX test vectors pass.
- [ ] Tampered, replayed and out-of-window messages are rejected.
- [ ] Both sides derive the same 6-digit code; any transcript change alters it.
- [ ] A handshake with Osplay's prologue fails (domain separation test).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
