# Phase 020 (MVP-20) — Packaging, release and documentation
Status: ACTIVE

DEVPLAN reference: Phase 19. Depends on: Phase 18

## Goal
Anyone can install Osdrop from GitHub Releases, and contributors can build it from the docs alone.

## Tasks
- [ ] Before the release, with a third computer: TESTING D1 Mac → Mac and PC → PC, D3 (chain) and D9 (moved from Phase 015).
- [x] macOS: Developer ID signing, Hardened Runtime, notarization with the `mac-notary` keychain profile and stapling, by one release script that uploads the DMG to the tag's release.
- [x] Windows: Inno Setup installer built by the release workflow (unsigned until signing exists) that installs the shell, engine, input process and service and adds the firewall rule; upgrades stop and restart the service; uninstall removes the service, the input process, the firewall rule and the Run entry and restores system cursors.
- [x] Release workflow triggered by a version tag; fixed asset names with SHA-256 files; daily update check in both apps.
- [x] Final `README.md` and `README.tr.md` in a plain tone (no emoji, no marketing headings): what it is and is not, how it works, screenshots, requirements, installation, first run, permissions, security summary, troubleshooting, building, contributing, license, credits. Repository description and topics written for search.
- [ ] Final `docs/` set.

## Acceptance Criteria
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine with no manual steps beyond SmartScreen.
- [x] Uninstall leaves no service, process, firewall rule or altered cursor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decisions (2026-11-21): release 0.1.0 now, with the release notes naming what was not
  tested (three-computer drag rows D1 Mac → Mac / PC → PC, D3, D9; robustness R5, R6, R9–R11; the
  two-hour run R12). Screenshots for the README come from the user, into docs/media.
- Done: 5131dc6 Windows installer and release workflow, 21a3a93 Mac app icon (it had none),
  92567ed Mac release script, 03a207f RELEASING.md, 011ffbb README.md and README.tr.md.
- 2026-11-21: v0.1.0 published (tag on main 35ec66d). The DMG is universal, macOS 14 or later,
  notarized and stapled; Gatekeeper accepted it on the M3 Air, which is not a clean Mac. Before
  publishing, the installer was run on the PC, which had a development service installed: install,
  connection, crossing and a drag worked, and uninstall left none of the above behind (TESTING
  Runs). Not run yet: an upgrade from one release to the next, a clean Windows 11, a clean Mac.
- Uninstalling keeps the user's folders on purpose (DECISIONS 2026-11-21).

