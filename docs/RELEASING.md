# Releasing

> Status: outline. The release script, the installer and the release workflow are written in
> Phase 19.

## Plan

- A version tag (`vX.Y.Z`) starts the release workflow, which builds the Windows installer and
  attaches it to a GitHub release with a SHA-256 file.
- The macOS DMG is built, signed with Developer ID, notarized with the `mac-notary` keychain
  profile and stapled on the maintainer's Mac by one script, which uploads it to the same release.
- No signing secret lives in CI.
- Both apps check the latest GitHub release once a day and offer the download.
