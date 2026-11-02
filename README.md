# Osdrop

[![CI](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml/badge.svg)](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Osdrop lets one keyboard and mouse move across several computers on the same local network. Push
the cursor through a screen edge and it continues on the neighbouring computer, with the keyboard.
Drag a file across the edge and it is copied where you drop it. Copy on one computer and paste on
another.

It works between macOS and Windows in every direction: Mac and PC, two Macs, two PCs, and up to
eight computers in one arrangement.

> **Status:** early development; nothing is usable yet. Progress follows the phases in
> [`DEVPLAN.md`](DEVPLAN.md).

## What it is and is not

- **Shared keyboard and mouse:** the computer whose mouse and keyboard you are using drives the
  others; each computer keeps its own monitors and its own desktop.
- **Files and clipboard:** dragged files and folders are copied to where they are dropped; text,
  images and files copied on one computer can be pasted on another.
- **Not screen sharing.** No picture of a screen is sent anywhere. For using one computer as an
  extra display of another, see [Osplay](https://github.com/akinalpfdn/osplay).
- **Local network only.** No accounts, no cloud, no relay servers.

## How it works

Paired computers form a group and keep encrypted connections to each other. When the cursor is
pushed through an edge, the computer you are using stops moving its own cursor and sends your
input to the neighbour, which applies it as if its own mouse and keyboard were used. Details:
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Screenshots

Coming with the first release.

## Requirements

- **macOS:** macOS 14 Sonoma or later.
- **Windows:** Windows 10 22H2 or Windows 11.
- All computers on the same local network; wired or Wi-Fi.

## Installation

### macOS

Coming with the first release (notarized DMG from GitHub Releases).

### Windows

Coming with the first release (installer from GitHub Releases).

## First run

Coming with the first release.

## Security

Computers pair once by confirming a 6-digit code on both screens; all traffic is end-to-end
encrypted. Each computer decides for itself which paired computers may control it, send it files
and share its clipboard. See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) and
[`SECURITY.md`](SECURITY.md).

## Troubleshooting

See [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

## Building from source

See [`docs/BUILDING.md`](docs/BUILDING.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) © 2026 Akinalp Fidan.

## Credits

Osdrop builds on [Monocypher](https://monocypher.org), [Asio](https://think-async.com/Asio/),
[WPF-UI](https://github.com/lepoco/wpfui), [H.NotifyIcon](https://github.com/HavenDV/H.NotifyIcon)
and [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet). Full list:
[`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
