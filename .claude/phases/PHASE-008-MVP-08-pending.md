# Phase 008 (MVP-08) — Sessions and mesh transport
Status: PENDING

DEVPLAN reference: Phase 7. Depends on: Phase 6

## Goal
A set of `core` instances keeps exactly one encrypted session per pair and carries control messages, input datagrams and file streams between them.

## Tasks
- [ ] Sans-I/O session engine (handshake, pairing confirmation, heartbeat, clock offset, stats) under a thin Asio transport, following Osplay's design.
- [ ] TCP control, UDP input datagrams with token association and latest-wins delivery, TCP transfer streams with tokens and per-stream keys.
- [ ] Mesh manager: dial direction by device ID, duplicate resolution, reconnect with backoff, peer-loss detection within one second for input safety; real-time traffic marking for input sockets.
- [ ] `capi` for the mesh, sessions and the log callback.

## Acceptance Criteria
- [ ] A simulated five-member group converges to a full mesh, also when every member dials at once.
- [ ] Motion datagrams under loss and reordering never move the cursor backwards.
- [ ] A saturated transfer stream does not delay control messages beyond a few milliseconds in the loopback test.
- [ ] Peer loss is detected within one second in simulation.
- [ ] No unencrypted bytes after the handshake (wire inspection test); ASan and TSan clean.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
