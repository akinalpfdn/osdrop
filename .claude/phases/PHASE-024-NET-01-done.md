# Phase 024 (NET-01) — Keep AWDL off while Osdrop runs (Mac)
Status: DONE (2026-11-20)

Not a DEVPLAN phase: user decision 2026-11-12, after Phase 15. See DECISIONS.md, "A Mac that sleeps leaves the mesh until it wakes; AWDL switch gets its own phase".

## Goal
An opt-in Mac setting keeps AWDL off while Osdrop runs, so Wi-Fi stays on the network's channel.

## Tasks
- [x] A privileged helper (launch daemon registered with SMAppService) that keeps awdl0 down while the app asks for it, and lets it up again when the app quits, crashes or turns the setting off.
- [x] The helper accepts requests only from Osdrop's own signed app.
- [x] Setting in the Mac app: off by default, explains what stops working (AirDrop, AirPlay, Sidecar), walks the user through the one-time approval, shows when approval is missing.
- [x] English and Turkish strings; the setting and its cost documented in the user docs.
- [x] Tests for the request/restore logic that does not need root.

## Acceptance Criteria
- [x] With the setting on, awdl0 stays down during a session on the reference Mac, and the Mac logs no "Peer unresponsive" in 30 min of normal use on Wi-Fi.
- [x] Turning the setting off, quitting Osdrop or killing it brings AWDL back within a few seconds.
- [x] With the setting off nothing changes for the user, and no approval is asked for.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 135b3d8 helper, 85f07d6 setting, 384d1c8 wording (user: "Wi-Fi bağlantısını iyileştir"), e99cef4 docs.
- 2026-11-20 21:08: turned on by the user on the reference Mac; awdl0 down, helper running. The
  30-minute acceptance run started then.
- Acceptance 2026-11-20: 21:08–21:38 no "Peer unresponsive", awdl0 down throughout; quit and
  kill -9 brought AWDL back within 1 s; the setting off at 21:43:34 brought it back, on again at
  21:43:41 took it down. Off by default: unit tests (AWDLSwitchTests).
