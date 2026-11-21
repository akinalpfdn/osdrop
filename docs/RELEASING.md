# Releasing Osdrop

A release has two downloads made in two places and published together as one GitHub release:

| Download | Built by | Signing |
|----------|----------|---------|
| `Osdrop-Setup.exe` (Windows) | the Release workflow on GitHub Actions | none (SmartScreen shows "More info → Run anyway") |
| `Osdrop.dmg` (macOS) | `scripts/macos/release.sh` on the maintainer's Mac | Developer ID, notarized and stapled |

The Developer ID certificate never leaves the maintainer's Mac, so the Mac part does not run on CI.
Each download has a `.sha256` file next to it.

## One-time setup on the Mac

- A **Developer ID Application** certificate in the login keychain (Xcode › Settings › Accounts ›
  Manage Certificates).
- A notarytool keychain profile named `mac-notary`, with an app-specific password from
  [account.apple.com](https://account.apple.com):

  ```sh
  xcrun notarytool store-credentials "mac-notary" --apple-id <Apple ID> --team-id <team ID> --password <app-specific password>
  ```

- `brew install xcodegen gh`, then `gh auth login`.
- `pip3 install dmgbuild`.

## Steps

1. Set the version `X.Y.Z` in both apps: `<Version>` in `windows/app/Directory.Build.props` and
   `MARKETING_VERSION` in `macos/project.yml`. `python3 scripts/release_version.py` checks that they
   agree. Merge the change through a pull request as usual.

2. Tag the merge commit and push the tag:

   ```sh
   git checkout main
   git pull
   git tag -a vX.Y.Z -m "Osdrop X.Y.Z"
   git push origin vX.Y.Z
   ```

3. Wait for the **Release** workflow (Actions tab). It checks that the tag matches both versions,
   builds and tests the Windows installer, and creates a **draft** release `vX.Y.Z` with generated
   notes, `Osdrop-Setup.exe` and `Osdrop-Setup.exe.sha256`.

4. On the Mac, at the tag:

   ```sh
   git checkout vX.Y.Z
   scripts/macos/release.sh
   ```

   The script refuses to run unless the working tree is clean, `HEAD` is the tagged commit and the
   draft already has the Windows installer. It archives the app, exports it with Developer ID,
   notarizes and staples the app, builds `Osdrop.dmg`, signs, notarizes and staples the DMG, checks
   both with Gatekeeper, uploads `Osdrop.dmg` and `Osdrop.dmg.sha256`, and publishes the release as
   the latest. With `--no-publish` it leaves the release a draft, for example to edit the notes;
   publish it afterwards with `gh release edit vX.Y.Z --draft=false --latest`.

5. Check the published release: both downloads from
   `https://github.com/akinalpfdn/osdrop/releases/latest/download/Osdrop.dmg` and
   `.../Osdrop-Setup.exe`, and the update check in an older version of each app (Settings › Check
   now) offers the new one.

## Rules

- Asset names carry no version, so `releases/latest/download/<name>` links (the README uses them)
  always reach the newest release.
- Do not mark releases as pre-releases, 0.x included: both apps' update checks read
  `/releases/latest`, which skips pre-releases.
- A published tag is never moved. A broken release is fixed by a new patch version.
- A failed workflow run can be re-run; it replaces the installer on the same draft.
- To try the installer without a release, run the Release workflow by hand (Actions › Release ›
  Run workflow) or open a pull request that changes the packaging; the installer is then a workflow
  artifact.
- Osdrop's development happens on `dev`; a release starts with a pull request from `dev` into
  `main`, after `scripts/lint.sh` passes and CI is green.
- Before a release, the rows of [`TESTING.md`](TESTING.md) that the release notes do not mark as
  untested have passed on the reference hardware.
