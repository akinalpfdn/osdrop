# Phase 020 (MVP-20) — Packaging, release and documentation
Status: PENDING

DEVPLAN reference: Phase 19. Depends on: Phase 18

## Goal
Anyone can install Osdrop from GitHub Releases, and contributors can build it from the docs alone.

## Tasks
- [ ] macOS: Developer ID signing, Hardened Runtime, notarization with the `mac-notary` keychain profile and stapling, by one release script that uploads the DMG to the tag's release.
- [ ] Windows: Inno Setup installer built by the release workflow (unsigned until signing exists) that installs the shell, engine, input process and service and adds the firewall rule; upgrades stop and restart the service; uninstall removes the service, the input process, the firewall rule and the Run entry and restores system cursors.
- [ ] Release workflow triggered by a version tag; fixed asset names with SHA-256 files; daily update check in both apps.
- [ ] Final `README.md` and `README.tr.md` in a plain tone (no emoji, no marketing headings): what it is and is not, how it works, screenshots, requirements, installation, first run, permissions, security summary, troubleshooting, building, contributing, license, credits. Repository description and topics written for search.
- [ ] Final `docs/` set.

## Acceptance Criteria
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine with no manual steps beyond SmartScreen.
- [ ] Uninstall leaves no service, process, firewall rule or altered cursor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
