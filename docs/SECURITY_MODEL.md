# Security model

> Status: the cryptography is implemented (Phase 6) and documented below. Group membership and the
> Windows input process follow in Phases 3, 7 and 8; until then their fixed decisions are in
> [`DEVPLAN.md`](../DEVPLAN.md) → Protocol Requirements → Security model.

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

## Cryptography

The wire format of everything below is in [`PROTOCOL.md`](PROTOCOL.md) → Encryption; the code is in
`core/src/crypto/` on top of [Monocypher](https://monocypher.org) 4.0.3.

- **Identity.** Each computer has one static X25519 key pair, created from the platform's secure
  random source (`SecRandomCopyBytes`, `BCryptGenRandom`). The app stores the 32-byte secret;
  `core` only receives it to run a handshake and never writes it anywhere. An all-zero secret is
  rejected on import. The device ID is derived from the public key, so it cannot be claimed
  without the matching secret.
- **Handshake.** Noise XX authenticates both static keys and gives forward secrecy through fresh
  ephemeral keys; the ephemeral secret is wiped as soon as the handshake ends. The prologue
  `Osdrop control v1` is mixed into the transcript, so a handshake with any other protocol that
  uses the same pattern (Osplay included) fails, and no key is ever shared between the two
  products. A low-order peer key (all-zero DH result), an out-of-order message, a truncated or
  oversized message or a failed authentication ends the handshake for good. The implementation
  reproduces the published cacophony and noise-c vectors byte for byte.
- **Pairing code.** Two computers that do not trust each other yet show a 6-digit code derived
  from the handshake hash, which covers both static keys, both ephemeral keys and the prologue. A
  man in the middle runs two separate handshakes and would need both codes to match: a chance of
  one in a million per attempt, which the user's confirmation on both screens turns into a
  rejected pairing.
- **Separate keys per channel and direction.** The control channel uses Noise's own transport
  keys; input datagrams and transfer streams use keys derived from the same final chaining key
  under their own labels. Each transfer stream has its own key per direction, derived from its
  stream ID; stream IDs are never reused within a session (odd for streams the initiator opens,
  even for the responder's), so no two streams ever share a key and nonce.
- **Replay and reordering.** Control frames and transfer frames carry an implicit counter: a
  replayed, dropped, reordered or altered frame fails authentication and ends the connection.
  Input datagrams carry their sequence number as the nonce and are accepted once each within a
  window of 1024; older or repeated ones are dropped. A forged datagram never moves the window.
- **Secrets in memory.** Key pairs, cipher states, session keys and intermediate HMAC and HKDF
  values are overwritten when they are no longer needed.

## Non-goals

- Protection against an attacker who already controls a computer in the group.
- Hiding that a computer runs Osdrop (Bonjour advertisements are visible on the LAN).
