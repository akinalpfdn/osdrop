# Security model

> Status: the cryptography (Phase 6), group membership (Phase 8) and the Windows input process
> (Phase 3) are implemented and documented below.

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

## Group membership

The rules are in [`PROTOCOL.md`](PROTOCOL.md) → Group membership; the code is in
`core/src/group/`.

- **Who is trusted.** A computer trusts exactly the members of its roster, identified by static
  key: the device ID is the key's fingerprint, and a roster entry whose ID does not match its key
  discards the whole roster. Core makes every trust decision; the platforms only store the state.
- **Who may change the roster.** Only current members, over their authenticated sessions. A
  computer outside the group cannot add itself by claiming the group's public ID, and a computer
  joins a group only from the roster of the computer it has just paired with, with its own key in
  it.
- **Introductions.** Pairing with any member is enough: the others trust the new computer because
  a member they already trust names it. Its user confirmed the code on both computers; no member
  accepts a stranger on another member's word alone, since rosters travel only between members.
- **Revocation.** A revoked epoch wins over every pairing up to it, in any merge order, so a member
  that was offline cannot bring a revoked computer back; only a new pairing, confirmed with a code,
  can. Each member refuses the revoked computer as soon as it has met any member that knows of the
  revocation. The revoked computer is told (Bye 6) and leaves the group, but a modified client
  could keep trying; members refuse it with NOT_TRUSTED.
- **Leaving.** A computer that leaves revokes itself, so the others stop dialling it.
- **Permissions.** Each computer enforces its own: it advertises only what it allows and refuses or
  drops what it denies, whatever the peer sends.
- **Limits.** One pairing prompt at a time (PEER_BUSY) and at most 8 members (GROUP_FULL) keep a
  stranger on the LAN from flooding the user with codes or growing a group without bound.
- **Blocks.** A member can block a computer, at its code or when removing it; the block travels in
  the roster and every member refuses that computer before any code until a member unblocks it. A
  computer that makes itself a new identity is a new device and is not blocked.
- **Stored state.** The group ID, roster and permissions are stored by the platform next to the
  identity key (Keychain, DPAPI). A state written by a newer Osdrop is refused, not overwritten.

## Windows input process

Capturing and injecting input over elevated windows (Task Manager, administrator terminals,
installers) needs a process above their integrity level. `uiAccess` would do it from the user's
own process but needs an Authenticode-signed binary, which the project does not have, so:

- **Privilege split.** `OsdropService` (LocalSystem) does only one thing: it starts `OsdropInput`
  in the active console session with its own SYSTEM token on `winsta0\default`, and moves it when
  the session changes. `OsdropInput` hooks, hides the cursor and injects, nothing else: it parses
  no network data, reads no files the user controls and links no networking code.
- **The pipe** (`\\.\pipe\com.akinalpfdn.osdrop.input.<session>`) is its only way in. Its ACL
  admits SYSTEM and the session's user only; it is created once per process with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` and never closed, so no other process can take the name; remote
  clients are rejected. A client must run in the same session, as the session's user (checked by
  identification-level impersonation), and be `Osdrop.exe` or `OsdropInputTool.exe` from
  `OsdropInput`'s own folder (checked by the client's process image), which only an administrator
  can write to once installed under Program Files.
- **Requests** are a small versioned set with every value bounds-checked (positions, buttons,
  wheel amounts, scan codes that exist on a keyboard). Injection is accepted only while the client
  controls this PC or captures its input, and whatever the client pressed is released when control
  or capture ends or the client goes. Invalid requests are refused and logged.
- **Capture safety.** A capture lasts only while the client confirms it every 250 ms; after a
  second without, or when the user presses Ctrl+Alt+Win+Esc, OsdropInput gives the input back by
  itself, so a hung Osdrop never keeps the mouse and keyboard.
- **Watching.** While a member is connected, Osdrop asks OsdropInput to report the PC's input as
  it happens, keys included, because core needs all of it to decide crossings and hold keys. This
  reaches Osdrop's own process, which runs as the user: keys typed into elevated windows, which a
  process of the user could not otherwise read, come to it too. Nothing is logged or stored, and
  the secure desktop (the UAC prompt, Ctrl+Alt+Del) is never seen.
- **Residual risk.** Malware running as the signed-in user that can write into the install folder
  (it cannot without administrator rights), or that controls Osdrop's own process, could drive
  elevated windows through the pipe and read the keys typed into them. Ctrl+Alt+Del and the secure desktop stay out of reach: Windows
  does not let any process inject them. Revisit with code signing, which would allow `uiAccess`
  and drop the SYSTEM process altogether.
- **Logging.** `%ProgramData%\Osdrop\logs\input.log` and `service.log` record starts, stops,
  refused clients and refused requests, never keys or positions.

## Non-goals

- Protection against an attacker who already controls a computer in the group.
- Hiding that a computer runs Osdrop (Bonjour advertisements are visible on the LAN).
