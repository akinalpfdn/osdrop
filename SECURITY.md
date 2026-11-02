# Security Policy

Osdrop lets paired computers type and click on each other, copies files between them and installs
a privileged Windows service, so security reports are taken seriously.

## Supported versions

Only the latest release receives security fixes. Osdrop has not had a release yet.

## Reporting a vulnerability

**Do not open a public issue.** Report privately through GitHub Security Advisories:

1. Open <https://github.com/akinalpfdn/osdrop/security/advisories/new>
   (or the repository's **Security** tab → **Report a vulnerability**).
2. Describe the issue, the affected platform and version, and steps to reproduce.
3. If you have a proof of concept, attach it to the advisory, not to a public place.

The report stays private between you and the maintainer until a fix is published. You will get a
reply in the advisory thread; please allow time for a fix before any disclosure.

## In scope

- The Noise handshake, pairing code, group membership, key storage and transport encryption in
  `core/`.
- Parsing of any data received from the network (control messages, input datagrams, transfer
  streams, file manifests, clipboard data).
- Input injection reaching a computer that did not allow control from the sender.
- File names or paths from another computer writing outside the drop location.
- The `OsdropService` Windows service, the `OsdropInput` process and its named pipe (privilege
  escalation, command injection, driving elevated windows from another process).
- The installers and the update check.

## Out of scope

- Attacks that require an already compromised computer in the group.
- Denial of service from a device on the same LAN flooding traffic.

See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) for the threat model.
