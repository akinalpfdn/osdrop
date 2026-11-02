# Security model

> Status: outline. The handshake, group membership and the Windows input process are specified and
> implemented in Phases 3 and 6–8; this page will then document them exactly. The fixed decisions
> are in [`DEVPLAN.md`](../DEVPLAN.md) → Protocol Requirements → Security model.

## Goals

- Only computers the user explicitly paired into the group can connect.
- Each computer decides for itself which members may control it, send it files and share its
  clipboard.
- Input, clipboard contents and files are confidential and tamper-evident on the LAN.
- A device on the same network cannot impersonate a member.
- The privileged Windows components cannot be driven by other users or by processes other than
  Osdrop's own app.

## Summary of the design

- **Handshake:** Noise `XX_25519_ChaChaPoly_BLAKE2b` with an Osdrop-specific prologue, implemented
  in `core` with Monocypher.
- **First pairing:** both computers show a 6-digit code derived from the handshake hash; the user
  confirms on both. The new computer joins the group, and existing members trust it through an
  introduction over their authenticated sessions.
- **Revocation:** reaches every member and cannot be undone by a member that was offline.
- **Transport:** control, input datagrams and transfer streams are encrypted with separate keys.
- **Windows privilege split:** only input capture and injection run as SYSTEM; networking and
  parsing run in the user's process.
- **No unencrypted mode** exists in release builds.
- **Key storage:** macOS Keychain; Windows DPAPI-protected file.
- **Logging:** keystrokes, clipboard contents, file contents and keys are never logged.

## Non-goals

- Protection against an attacker who already controls a computer in the group.
- Hiding that a computer runs Osdrop (Bonjour advertisements are visible on the LAN).
