# Phase 007 (MVP-07) — Crypto and handshake
Status: DONE

DEVPLAN reference: Phase 6. Depends on: Phase 5

## Goal
Two `core` instances authenticate each other, show the same pairing code and derive keys for control, input datagrams and transfer streams.

## Tasks
- [x] Monocypher vendored with its license.
- [x] Noise XX, cipher states, replay window and platform-supplied randomness copied from Osplay at a recorded commit, with an Osdrop prologue and labels; key derivation for input datagrams and per-stream transfer keys; pairing code.
- [x] `capi` for identity generation, import and export (storage stays on the platforms).

## Acceptance Criteria
- [x] Published Noise XX test vectors pass.
- [x] Tampered, replayed and out-of-window messages are rejected.
- [x] Both sides derive the same 6-digit code; any transcript change alters it.
- [x] A handshake with Osplay's prologue fails (domain separation test).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Crypto copied from Osplay `35046970`; Monocypher 4.0.3 checked against the release archive.
- Transfer stream IDs: odd from the initiator, even from the responder, never reused (prevents key and nonce reuse).
- `derive_session_keys` is separate from the handshake so labels and HKDF order are checked against Python-computed values.
