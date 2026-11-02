# Testing

> Status: outline. The manual test matrix grows with every phase that touches real hardware and is
> complete in Phase 18.

## Automated tests

Run by CI on every pull request (commands in [`BUILDING.md`](BUILDING.md)):

| Suite | Framework | Covers |
|-------|-----------|--------|
| `core/tests/cpp` | GoogleTest | Every `core` module |
| `windows/shared/tests` | GoogleTest | Windows-header-free libraries, on macOS and Windows |
| `macos/CoreBridgeTests` | Swift Testing | The Swift wrapper over the C API |
| `macos/OsdropTests` | Swift Testing | Mac app logic |
| `windows/app/Osdrop.Localization.Tests` | xUnit | Shell strings in every shipped language |

The core also runs under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer on macOS.

## Hardware tests

Tests that move the real cursor, type, or start drags cannot run in CI. They live in separate
schemes and developer tools and are run by hand on the reference hardware: a MacBook Air M3, a
second Mac, a Windows PC with two monitors and a second Windows computer, wired and on Wi-Fi.

## Manual matrix

Added from Phase 1 on.
