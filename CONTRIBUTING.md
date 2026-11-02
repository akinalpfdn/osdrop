# Contributing to Osdrop

Thanks for your interest. Osdrop is a small project with firm design rules; reading this page first
saves review round-trips.

## Before you start

- **Scope:** Osdrop shares one keyboard and mouse, files and the clipboard between computers on the
  same local network. Screen sharing and control over the internet will not be added. The roadmap
  lives in [`DEVPLAN.md`](DEVPLAN.md).
- **Bugs and features:** open an issue with the matching template before large changes, so the
  approach can be agreed first.
- **Security issues:** never in public issues — see [`SECURITY.md`](SECURITY.md).
- By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Development setup

Follow [`docs/BUILDING.md`](docs/BUILDING.md) exactly. It covers macOS (terminal) and Windows
(command line) and is kept accurate by CI.

## Workflow

1. Branch from `main`: `feature/<short-name>`, `fix/<short-name>`, `docs/<short-name>`.
2. Keep each branch to one concern. Refactors and features go in separate pull requests.
3. Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
   `feat(core): add crossing detection`, `fix(macos): release held keys on disconnect`.
   Lowercase, imperative, no trailing period, at most 72 characters in the subject.
4. Run the checks before pushing:
   - macOS: `scripts/lint.sh` (formatting, clang-tidy, UI tokens, localization completeness).
   - Windows: `dotnet format windows/app/Osdrop.slnx --verify-no-changes`.
5. Open a pull request using the template. CI must be green before review.

## Code rules

These are enforced in review and, where possible, by tooling:

- **Input safety first.** Every injected key or button press is released on every exit path, and
  capture always has a way back to the user. A change that weakens this is a bug.
- **No hardcoded user-facing text.** Every string goes through `Localizable.xcstrings` (macOS) or
  `.resx` (Windows) and must exist in **English and Turkish**. `core`, the engine and the input
  process return error codes, never text. `scripts/check_localization.py` fails on missing
  translations.
- **No hardcoded visual values.** Colors, fonts, spacing, radii and shadows come from the platform
  theme (`macos/Osdrop/Theme`, `windows/app/Osdrop.App/Theme`); `scripts/check_ui_tokens.sh`
  enforces it.
- **No hardcoded tunables.** Protocol constants live in `core`; timeouts and limits live in one
  constants file per platform.
- **Errors are explicit.** No exceptions across the C API, no silent catches, no force unwraps in
  Swift outside tests.
- **Network input is hostile.** Validate every length and index before use.
- **Private data stays private.** Never log keystrokes, clipboard contents, file contents or keys.
- Comments explain *why*, not *what*.

## Tests

- Every `core` module has unit tests (GoogleTest; Swift tests for the bridge).
- Every bug fix comes with a test that fails before the fix.
- Hardware paths (capture, injection, drag and drop) are covered by hardware test schemes and the
  manual matrix in `docs/TESTING.md`.

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).
