# Phase 024 (NET-01) — Keep AWDL off while Osdrop runs (Mac)
Status: PENDING

Not a DEVPLAN phase: user decision 2026-11-12, after Phase 15. See DECISIONS.md, "A Mac that sleeps leaves the mesh until it wakes; AWDL switch gets its own phase".

## Goal
An opt-in Mac setting keeps AWDL off while Osdrop runs, so Wi-Fi stays on the network's channel.

## Tasks
- [ ] A privileged helper (launch daemon registered with SMAppService) that keeps awdl0 down while the app asks for it, and lets it up again when the app quits, crashes or turns the setting off.
- [ ] The helper accepts requests only from Osdrop's own signed app.
- [ ] Setting in the Mac app: off by default, explains what stops working (AirDrop, AirPlay, Sidecar), walks the user through the one-time approval, shows when approval is missing.
- [ ] English and Turkish strings; the setting and its cost documented in the user docs.
- [ ] Tests for the request/restore logic that does not need root.

## Acceptance Criteria
- [ ] With the setting on, awdl0 stays down during a session on the reference Mac, and the Mac logs no "Peer unresponsive" in 30 min of normal use on Wi-Fi.
- [ ] Turning the setting off, quitting Osdrop or killing it brings AWDL back within a few seconds.
- [ ] With the setting off nothing changes for the user, and no approval is asked for.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
