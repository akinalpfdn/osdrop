# Osdrop wire protocol

Version 1.0. This document is authoritative: `core/src/protocol/` implements it, and
`protocol/tools/generate_fixtures.py` independently encodes the binary fixtures in
`protocol/fixtures/` from it. Both implementations must agree byte for byte.

The protocol is not frozen before the first release. A phase that changes a message changes this
document, the fixture generator and the codec in the same pull request. From the first release on,
changes are additive and raise the minor version.

## Conventions

- All integers are **little-endian**, unsigned unless written `i32`/`i64`.
- `bytes[N]` is exactly N bytes.
- `string` is `u8 length` followed by that many bytes: at most **64 bytes**, valid UTF-8 (no
  overlong forms or surrogates), no control characters (U+0000–U+001F, U+007F–U+009F).
- `type_id` is `u8 length` followed by 1–128 bytes of a Uniform Type Identifier: ASCII letters,
  digits, `.`, `-` and `_` only.
- `name` is `u16 length` followed by 1–255 bytes: one file or folder name, valid UTF-8, no control
  characters, no `/`, not `.` or `..`. Senders write names in Unicode Normalization Form C;
  receivers normalize again before using a name, so decoders do not check the form.
- `path` is `u16 length` followed by 1–1024 bytes: names (as above) joined with `/`, relative,
  no empty component.
- `coord` is an `i32` in 1/16 of a unit, from −2^28 to 2^28 (±16,777,216 units), so the sum of
  three coordinates fits an `i32`. The unit is the *logical* one (macOS points, Windows
  device-independent pixels) unless a field says *native*: the units the computer's own cursor
  moves in (macOS points, Windows physical pixels).
- `device_id` is `bytes[16]`: the first 16 bytes of BLAKE2b-256 over the device's static public
  key.
- Fields marked *reserved* are written as 0 and must be 0 when read.
- Decoders are strict: a message must be exactly as long as its fields; trailing bytes, unknown
  enum values and out-of-range values are errors. Counts are bounded, but a message with variable
  parts must also fit its channel's size limit; an encoder fails with `TOO_LARGE` otherwise. An
  encoder refuses to produce anything a decoder would reject.
- Times are microseconds on the sender's monotonic clock (`*_us`).

## Layers

```
Discovery          Bonjour / DNS-SD  _osdrop._tcp
Control (TCP)      frame = u32 length | payload
                   payload = control message           (before the handshake completes)
                   payload = AEAD(control message)     (after)
Input (UDP)        datagram = input header (12 bytes, in clear) | AEAD(input body)
Transfer (TCP)     a separate connection per transfer stream, same framing as control:
                   first frame TransferHello in clear, then AEAD(transfer message)
```

The messages below are defined in plaintext; "Encryption" specifies how they are protected on the
wire. The protocol fixtures are plaintext.

### Common prefix

Every control message, input datagram and transfer message starts with the same three bytes:

| Offset | Field | Type | Value |
|-------:|-------|------|-------|
| 0 | magic | bytes[2] | `4F 44` ("OD") |
| 2 | version | u8 | protocol major version, **1** |

A receiver that reads a different version stops decoding (`UNSUPPORTED_VERSION`) and the session
ends with "update the other computer". The prefix never changes across versions.

## Discovery

Service type `_osdrop._tcp`; the SRV record carries the TCP port of the control listener, which
also accepts transfer connections. TXT record keys:

| Key | Value |
|-----|-------|
| `pv` | protocol major version, decimal (`1`) |
| `pm` | protocol minor version, decimal (`0`) |
| `id` | device ID, 32 lowercase hex digits |
| `name` | device name (UTF-8, as `string`) |
| `os` | `macos` or `windows` |
| `grp` | group ID, 32 lowercase hex digits (never all zeros), or empty when the computer is in no group |

Numbers are decimal without sign or leading zeros. A record with a different `pv` is still
parsed, so the UI can ask the user to update the other computer.

## Control channel (TCP)

Each frame is `u32 length` followed by `length` bytes of payload, 1 ≤ length ≤ **65536** for
plaintext messages (65552 with the 16-byte AEAD tag). A control message is:

| Offset | Field | Type |
|-------:|-------|------|
| 0 | magic | bytes[2] |
| 2 | version | u8 |
| 3 | type | u8 |
| 4 | body | type-specific, to the end of the payload |

| Type | Name | When |
|-----:|------|------|
| 0x01 | Hello | first message on a control connection, plaintext |
| 0x02 | HandshakeMessage | Noise XX messages 1–3, plaintext |
| 0x03 | PairingConfirm | once per side after the handshake |
| 0x04 | Capabilities | after pairing |
| 0x05 | SessionSetup | after pairing: the sender's ports and tokens |
| 0x06 | Ping | RTT and clock offset |
| 0x07 | Pong | reply to Ping |
| 0x08 | Heartbeat | keeps an idle session alive |
| 0x09 | Error | before closing on an error |
| 0x0A | Bye | orderly close |
| 0x10 | RosterState | the sender's view of the group |
| 0x11 | ScreenReport | the sender's own monitors |
| 0x12 | LayoutState | the sender's view of the shared arrangement |
| 0x20 | Enter | controller → endpoint: input now goes to the endpoint |
| 0x21 | Leave | controller → endpoint: input left the endpoint |
| 0x22 | Refuse | endpoint → controller: the endpoint does not take input |
| 0x23 | KeyEvent | controller → endpoint |
| 0x24 | ButtonEvent | controller → endpoint |
| 0x25 | ScrollEvent | controller → endpoint |
| 0x26 | ReleaseAll | controller → endpoint: release every key and button |
| 0x27 | CursorReport | endpoint → controller: where the cursor really is |
| 0x28 | LocalTakeover | endpoint → controller: the endpoint's own user took over |
| 0x30 | DragProbe | controller → computer under the cursor: is a drag in progress there? |
| 0x31 | DragOffer | reply to DragProbe, and to the drop target when the drag crosses |
| 0x32 | DragResult | drop target → source and controller; controller → source when no target ends it |
| 0x40 | ClipboardAnnounce | the sender's clipboard changed |

### 0x01 Hello

| Field | Type | Values |
|-------|------|--------|
| platform | u8 | 1 macOS, 2 Windows |
| protocol_minor | u8 | minor version of the sender (0) |
| reserved | u16 | 0 |
| group_id | bytes[16] | the sender's group, all zeros when in no group |
| device_name | string | 1–64 bytes |

### 0x02 HandshakeMessage

| Field | Type | Values |
|-------|------|--------|
| step | u8 | 1, 2 or 3 (Noise XX message number) |
| reserved | u8 | 0 |
| payload_length | u16 | 1–1024 |
| payload | bytes[payload_length] | Noise handshake message |

### 0x03 PairingConfirm

| Field | Type | Values |
|-------|------|--------|
| decision | u8 | 1 accepted, 2 rejected |

Each side sends exactly one: at once when it already trusts the peer, after its user decides
otherwise (as in Osplay).

### 0x04 Capabilities

| Field | Type | Values |
|-------|------|--------|
| features | u32 | bit 0 takes control (injects input); bit 1 drag source; bit 2 drag target; bit 3 clipboard; bit 4 files. Other bits reserved. |
| max_transfer_streams | u8 | 1–8 |
| reserved | bytes[3] | 0 |

A feature bit says what the sender can do and allows; each computer's local permissions clear
the bits it refuses for this peer (see "Group membership"). Capabilities is sent once while the
session is set up and again, in the established session, whenever those permissions change; the
newest replaces the previous one.

### 0x05 SessionSetup

| Field | Type | Values |
|-------|------|--------|
| udp_port | u16 | 1–65535: the sender's input socket for this session |
| listen_port | u16 | 1–65535: the sender's TCP listener, where the peer opens transfer streams |
| input_token | bytes[16] | proves an InputHello comes from this session's peer |
| transfer_token | bytes[16] | proves a TransferHello comes from this session's peer |

Tokens are random and never all zeros.

### 0x06 Ping

| Field | Type |
|-------|------|
| ping_id | u32 |
| send_time_us | u64 |

### 0x07 Pong

| Field | Type |
|-------|------|
| ping_id | u32 |
| ping_send_time_us | u64 |
| receive_time_us | u64 |
| reply_time_us | u64 |

### 0x08 Heartbeat

No body.

### 0x09 Error

| Field | Type | Values |
|-------|------|--------|
| code | u16 | an error code from "Error codes", 1 or more |

### 0x0A Bye

| Field | Type | Values |
|-------|------|--------|
| reason | u8 | 1 user disconnected, 2 application quit, 3 going to sleep, 4 error, 5 leaving the group, 6 revoked, 7 duplicate connection (see "Session flow") |

### 0x10 RosterState

The sender's view of the group: for each device it knows of, the newest pairing and the newest
revocation. Receivers merge it as described in "Group membership".

| Field | Type | Values |
|-------|------|--------|
| group_id | bytes[16] | not all zeros |
| member_count | u8 | 1–64 |
| revocation_count | u8 | 0–255 |
| members | member × member_count | |
| revocations | revocation × revocation_count | |

member:

| Field | Type | Values |
|-------|------|--------|
| device_id | bytes[16] | |
| public_key | bytes[32] | X25519 static public key |
| epoch | u32 | 1 or more; each new pairing of the same device has a higher epoch |
| platform | u8 | 1 macOS, 2 Windows |
| reserved | u8 | 0 |
| name | string | 1–64 bytes |

revocation:

| Field | Type | Values |
|-------|------|--------|
| device_id | bytes[16] | |
| epoch | u32 | 1 or more |

No two members share (device_id, epoch); neither do two revocations. Whether `device_id` matches
`public_key` is checked by the group logic, not the codec: one mismatch discards the whole message.
A roster too large for one message is split into several, each with at least one member.

### 0x11 ScreenReport

| Field | Type | Values |
|-------|------|--------|
| monitor_count | u8 | 1–16 |
| monitors | monitor × monitor_count | |

monitor:

| Field | Type | Values |
|-------|------|--------|
| key | bytes[16] | stable identity of the monitor on its computer |
| x | coord | frame in the computer's logical desktop space |
| y | coord | |
| width | coord | 16 (one unit) to 16 × 32768 |
| height | coord | as width |
| native_x | coord | the same monitor in native units, where the computer's cursor moves |
| native_y | coord | |
| native_width | coord | as width |
| native_height | coord | as width |
| pixel_width | u16 | 1–32768 |
| pixel_height | u16 | 1–32768 |
| flags | u8 | bit 0 primary display. Other bits reserved. |
| reserved | u8 | 0 |

Keys are unique; exactly one monitor is primary. On macOS the native rectangle is the frame. On
Windows it is the monitor in physical pixels, and the frames are a logical arrangement the sender
derives from it (see "Arrangement"), because Windows has no logical desktop when monitors use
different scales.

### 0x12 LayoutState

| Field | Type | Values |
|-------|------|--------|
| revision | u64 | the arrangement with the higher (revision, author) wins |
| author | device_id | the member that made this revision |
| placement_count | u8 | 1–64 |
| placements | placement × placement_count | |

placement:

| Field | Type | Values |
|-------|------|--------|
| device_id | bytes[16] | |
| x | coord | where the device's desktop origin sits in the shared arrangement |
| y | coord | |
| flags | u8 | bit 0 detached (not reachable by crossing). Other bits reserved. |
| reserved | bytes[3] | 0 |

Device IDs are unique.

### Input control messages

Every input message carries a `control_id`: the controller picks a new non-zero value with each
Enter, and the endpoint ignores input messages and datagrams with any other value. A Leave or a
new Enter ends the previous control_id.

`modifiers` lists held modifier keys in HID order: bit 0 left Control (usage 0xE0), bit 1 left
Shift, bit 2 left Option/Alt, bit 3 left Command/Windows, bit 4 right Control, bit 5 right Shift,
bit 6 right Option/Alt, bit 7 right Command/Windows (0xE7).

Positions (`x`, `y`) are native positions in the endpoint's desktop (see ScreenReport).

An endpoint follows one controller at a time. Another computer's Enter while it is controlled, or
while its own input is on another computer, gets Refuse 4 (busy); one while its screen is locked,
it is going to sleep or it may not inject input gets Refuse 1, 2 or 3. When that happens during
control, the endpoint sends Refuse with the current `control_id` and releases what it holds. A
Refuse or LocalTakeover with the current `control_id` ends the controller's capture, and its
cursor reappears where it left.

An endpoint injects the release of a key or button only if it pressed it, and releases everything
it holds on Leave, ReleaseAll (the control goes on), physical input of its own (it sends
LocalTakeover), and a controller that stops answering or leaves. It sends CursorReport when
something other than the controller moved its cursor by more than one unit.

#### 0x20 Enter

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| x | coord | entry point |
| y | coord | |
| modifiers | u8 | held when crossing |
| locks | u8 | bit 0 Caps Lock on at the controller; other bits 0 |
| reserved | bytes[2] | 0 |
| drag_id | u32 | 0, or the drag carried across (see DragOffer) |

Caps Lock travels as a state: the endpoint sets it from `locks` on Enter and turns it over on each
KeyEvent down of usage 0x39, which it never injects as a key press. A controller that comes back
home sets its own Caps Lock to the state it left the endpoints in.

Between a Mac and a PC, a controller swaps Control and Command/Windows (usages 0xE0 ↔ 0xE3,
0xE4 ↔ 0xE7) in Enter's `modifiers` and in KeyEvents by default, so shortcuts keep their place;
the user can turn this off. The choice holds for a whole entry, so every release matches its
press. Endpoints inject what they receive.

#### 0x21 Leave

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| reason | u8 | 1 crossed on, 2 returned to the controller, 3 released by hotkey, 4 controller lost its input |

#### 0x22 Refuse

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| reason | u8 | 1 locked, 2 asleep, 3 not permitted, 4 busy |

#### 0x23 KeyEvent

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| page | u16 | 7 keyboard, 12 consumer |
| usage | u16 | not 0 |
| action | u8 | 1 down, 2 up, 3 repeat |
| reserved | u8 | 0 |

#### 0x24 ButtonEvent

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| button | u8 | 0 left, 1 right, 2 middle, 3–31 others |
| action | u8 | 1 down, 2 up |
| click_count | u8 | 1 or more: which click of a multi-click this is, an up repeating its down's count |
| reserved | u8 | 0 |
| x | coord | the endpoint moves the cursor here first |
| y | coord | |

The controller counts clicks from the timing it captured, with its own double-click settings, so
network delay never splits a double-click. macOS endpoints set the event's click state from
`click_count`; Windows derives double-clicks from the injected events itself.

#### 0x25 ScrollEvent

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| unit | u8 | 1 lines (wheels), 2 points (trackpads, continuous wheels) |
| phase | u8 | 0 none, 1 may begin, 2 began, 3 changed, 4 ended, 5 cancelled |
| momentum | u8 | 0 none, 1 began, 2 changed, 3 ended |
| reserved | u8 | 0 |
| dx | i32 | 1/256 of a unit; positive moves content right |
| dy | i32 | 1/256 of a unit; positive moves content down |
| x | coord | where the scroll happens |
| y | coord | |

A line is one notch of a wheel on every platform, so each computer scrolls a notch as its own
settings say; a point is a trackpad point (on Windows, 1/48 of a notch). Endpoints apply the
content movement as it is, whatever their own scroll direction setting.

#### 0x26 ReleaseAll

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |

#### 0x27 CursorReport

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| x | coord | |
| y | coord | |

#### 0x28 LocalTakeover

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |

### Drag messages

A drag of files and folders starts on its **source** computer, with the left button (0) held,
and the **controller** carries it to a **target**. Contents travel on transfer streams: object kind
1, `object_id` = drag_id, `item_index` = the item's place in the DragOffer.

- **Probe.** While a button is held every edge is a wall. When the cursor pushes against an edge
  that has a neighbour, with only the left button held, the controller asks the computer under the
  cursor whether a drag is in progress there: DragProbe, or nothing when that is itself. A computer
  answers with a DragOffer carrying the probe's ID: drag_id 0 when it has no drag, or when the
  prober does not control it now. The answer may wait until the items are measured, and for a
  drag of files that exist only once it is dropped (promised files, as from Photos) until the
  source has taken them at the edge; that drag then ends on the source and never slides back. The
  controller probes again while the push goes on, one probe at a time.
- **Crossing.** With a drag, the edges stop being walls toward computers that take drags
  (Capabilities bit 2) and toward the source; the push goes on and the cursor crosses. The
  controller sends the target the DragOffer (probe_id 0, the same drag_id, source and items), then
  Enter with the drag_id. An Enter whose drag_id has no forwarded DragOffer from the same
  controller before it is refused (Refuse 3).
- **Parking.** The source keeps the left button held when the entry of a controller it offered the
  drag to ends: the drag waits at the edge for its outcome.
- **Target.** If the target is the source, its parked drag goes on. Otherwise it starts a drag of
  the items at the entry point, pressing the left button there, and fetches the items from the
  source when the drop asks for them. The release of the left button drops the drag; the target
  sends DragResult to the source and to the controller, those that are not itself. When the
  controller's entry ends (Leave), the target ends its drag without a drop and sends nothing: the
  controller carries the drag on. When the target ends the control itself (its own mouse or
  keyboard, a lock screen, a permission), it sends DragResult 2.
- **Home.** When the cursor comes back to the controller, a drag the controller started goes on.
  A drag from another computer makes the controller its target when the cursor came back through
  an edge; otherwise (the hotkey, a refusal, a lost computer) the controller sends DragResult 2 to
  the source.
- **End.** The controller stops carrying the drag when the left button goes up or a DragResult
  for it arrives. The source ends its parked drag at the DragResult: as a copy for 1, sliding back
  for 2 and 3. It slides it back on its own when it loses the controller or its own mouse or
  keyboard is used. After a drop it keeps the items available for the transfers; a drag that
  ended any other way is withdrawn.

#### 0x30 DragProbe

| Field | Type | Values |
|-------|------|--------|
| probe_id | u32 | not 0 |
| x | coord | where the cursor reached the edge |
| y | coord | |

#### 0x31 DragOffer

| Field | Type | Values |
|-------|------|--------|
| probe_id | u32 | the DragProbe answered, or 0 when forwarded to a target (drag_id is then not 0) |
| drag_id | u32 | 0 when no drag is in progress, otherwise unique per source |
| source | device_id | the computer the drag started on; zero when drag_id is 0, otherwise not zero |
| item_count | u16 | 0 when drag_id is 0, otherwise 1–1024; the message must still fit in 65536 bytes |
| items | item × item_count | |

item:

| Field | Type | Values |
|-------|------|--------|
| kind | u8 | 1 file, 2 folder |
| reserved | u8 | 0 |
| byte_count | u64 | the file's size, or the total of the files inside the folder |
| content_type | type_id | |
| name | name | |

#### 0x32 DragResult

| Field | Type | Values |
|-------|------|--------|
| drag_id | u32 | not 0 |
| outcome | u8 | 1 dropped (the source ends its drag as a copy), 2 cancelled (the source's drag slides back), 3 failed (the target could not start the drag) |

Sent by the target, and by the controller when the drag ends without one (see above).

### 0x40 ClipboardAnnounce

Formats only; contents travel on a transfer stream when someone pastes (DEVPLAN, clipboard).

| Field | Type | Values |
|-------|------|--------|
| clip_id | u64 | not 0; a new value for every change |
| format_count | u8 | 1–5 |
| formats | format × format_count | |

format:

| Field | Type | Values |
|-------|------|--------|
| format | u8 | 1 plain text (UTF-8), 2 rich text (RTF), 3 HTML (UTF-8), 4 image (PNG), 5 files |
| reserved | bytes[3] | 0 |
| byte_count | u64 | size of the content in this format; for files, the total |

Formats are unique.

**The newest copy.** `clip_id` is the time of the copy in microseconds since 1970-01-01 UTC, but
never at or below a clip ID the sender has already seen (from any member): then it is one above
the highest. A member keeps the copy with the highest (`clip_id`, device ID) pair, device IDs
compared byte by byte, and ignores announcements of older ones; so members that hear of the same
copies agree on the newest, and a copy made after hearing of another one always wins. A sender
announces a copy only to the members it shares clipboards with, once (members offline at the time
miss it). It serves the formats from memory and files from a scan made at the copy; it withdraws
them when another member's newer copy arrives, so requests for an older copy get NOT_FOUND.

**Fetching.** Formats 1-4 are fetched as single contents: a ContentRequest for entry 0 with no
manifest (a receiver asks for one byte more than it accepts, so it can tell a content that is too
large). Format 5 is fetched like a dragged folder: manifest, then contents. Receivers fetch only
from members whose clipboard permission is on, and files only when the files permission is on too.

## Input datagrams (UDP)

A datagram is at most **1200 bytes**. It consists of a header that travels in clear (it is the
AEAD's associated data and its sequence number forms the nonce) followed by the body, which is
encrypted and followed by a 16-byte tag on the wire.

Input header (12 bytes):

| Offset | Field | Type | Values |
|-------:|-------|------|--------|
| 0 | magic | bytes[2] | `4F 44` |
| 2 | version | u8 | 1 |
| 3 | type | u8 | see below |
| 4 | sequence | u64 | per sender and session, starts at 0, +1 per datagram |

| Type | Name | Direction |
|-----:|------|-----------|
| 0x01 | InputHello | to the peer's input socket, until it answers |
| 0x02 | PointerMotion | controller → endpoint |

### 0x01 InputHello

| Field | Type |
|-------|------|
| input_token | bytes[16], from the peer's SessionSetup |

The receiver learns the sender's UDP endpoint from the first valid InputHello and answers with its
own.

### 0x02 PointerMotion

| Field | Type | Values |
|-------|------|--------|
| control_id | u32 | not 0 |
| x | coord | the new cursor position in the endpoint's desktop space |
| y | coord | |
| capture_time_us | u64 | controller clock when the movement was captured |

Motion is latest-wins: a receiver drops a datagram whose sequence is lower than the last one it
applied.

## Transfer streams (TCP)

A transfer stream is a TCP connection from the receiving side to the sending side's control
listener. It uses the control channel's framing (maximum plaintext 65536 bytes). The first frame
is a TransferHello in clear; every later frame is encrypted with the stream's keys. A transfer
message has the common prefix and a type. Transfer types (0x81–0x88) never overlap control types
(below 0x80), so the listener tells a transfer stream from a control connection by the type of the
first frame.

| Type | Name | Direction |
|-----:|------|-----------|
| 0x81 | TransferHello | receiver → sender, plaintext |
| 0x82 | ManifestRequest | receiver → sender |
| 0x83 | Manifest | sender → receiver |
| 0x84 | ContentRequest | receiver → sender |
| 0x85 | ContentChunk | sender → receiver |
| 0x86 | ContentEnd | sender → receiver |
| 0x87 | TransferError | both |
| 0x88 | TransferCancel | receiver → sender |

Objects: `object_kind` 1 is a drag (`object_id` = drag_id, `item_index` = the item in the
DragOffer, 0–1023), 2 is a clipboard announcement (`object_id` = clip_id, `item_index` = the
format code, 1–5). Clipboard formats 1–4 are single contents; format 5 (files) has a manifest like
a dragged item.

### 0x81 TransferHello

| Field | Type | Values |
|-------|------|--------|
| transfer_token | bytes[16] | from the sender's SessionSetup |
| stream_id | u32 | not 0; odd when the session's initiator opens the stream, even when the responder does; never reused within a session. Selects the stream's keys. |

### 0x82 ManifestRequest

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |
| object_kind | u8 | 1 drag, 2 clipboard |
| reserved | bytes[3] | 0 |
| object_id | u64 | not 0 |
| item_index | u32 | |

### 0x83 Manifest

One part of the list of entries an item consists of. A file item has one entry; a folder lists
every folder, file and link inside it, parents before children.

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |
| is_last | u8 | 0 or 1 |
| reserved | u8 | 0 |
| entry_count | u16 | 1–4096; the message must still fit in 65536 bytes, so senders split long lists by size |
| entries | entry × entry_count | |

entry:

| Field | Type | Values |
|-------|------|--------|
| path | path | relative to the item; a file item's single entry is its name |
| kind | u8 | 1 file, 2 folder, 3 symbolic link |
| flags | u8 | bit 0 executable, files only. Other bits reserved. |
| reserved | u16 | 0 |
| byte_count | u64 | file: its size. folder: 0. link: the length of its target, 1–4096 |
| modified_us | i64 | modification time, microseconds since 1970-01-01 UTC |

A link's content is its target path as UTF-8 bytes in NFC with `/` separators, requested like a
file's; receivers recreate a link only when its target names another entry of the same item
("Transfers" below).

### 0x84 ContentRequest

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |
| object_kind | u8 | 1 drag, 2 clipboard |
| reserved | bytes[3] | 0 |
| object_id | u64 | not 0 |
| item_index | u32 | |
| entry_index | u32 | the entry in the item's manifest; 0 for clipboard formats 1–4 |
| offset | u64 | first byte wanted |
| length | u64 | bytes wanted, 1 or more; may run past the end, which ends the content |

### 0x85 ContentChunk

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |
| offset | u64 | of the first byte in `data` |
| data_length | u32 | 1–65504 |
| data | bytes[data_length] | |

Chunks of one request arrive in order and without gaps.

### 0x86 ContentEnd

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |
| byte_count | u64 | bytes sent for the request |
| digest | bytes[32] | BLAKE2b-256 of the whole entry (or clipboard format), not only the requested range |

### 0x87 TransferError

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | 0 when the error concerns the whole stream |
| code | u16 | an error code, 1 or more |

### 0x88 TransferCancel

| Field | Type | Values |
|-------|------|--------|
| request_id | u32 | not 0 |

## Session flow

### Connections

- Every computer runs one TCP listener, the port in its SRV record. It accepts control connections
  and transfer streams; the type of the first frame tells them apart.
- Two members of a group keep exactly one session. The member with the lower device ID (compared
  byte by byte) dials as soon as it discovers the other; the higher one dials too only when no
  session exists 3 s after discovery. A computer dials only peers that advertise its own,
  non-empty group.
- When a second connection between the same pair authenticates, both sides keep the same one: if
  the same side dialled both, the newer one; otherwise the one the lower device ID dialled. The
  other is closed with Bye 7, which ends it without an error and without a reconnect.
- A dialled peer must authenticate with the static key of the device ID it advertised; any other key
  is refused with NOT_TRUSTED.
- After a connection ends with an error, the dialling side tries again after 250 ms, doubling up to
  30 s, until a session is established. A peer that refused (NOT_TRUSTED, PAIRING_REJECTED,
  UNSUPPORTED_VERSION) or that this computer disconnected on purpose is not dialled again until it
  is discovered again.

### Control connection

1. Both sides send Hello.
2. The dialler sends HandshakeMessage step 1 once the listener's Hello arrived; steps 2 and 3
   follow.
3. Each side decides about the authenticated peer (see "Group membership" → Pairing): a member
   gets PairingConfirm *accepted* at once; an unknown computer gets the pairing code shown and
   PairingConfirm after the user's answer; a refused one gets Error with the reason.
4. When both have accepted, each side sends Capabilities and SessionSetup. The session is
   *established* once the peer's two have arrived.
5. Application messages (types 0x10 and above) travel only in an established session.

Before the handshake completes only Hello, HandshakeMessage and Error travel, in plaintext; after
it every frame is encrypted. A message that is malformed or arrives in the wrong state ends the
session, and the peer is told with Error (UNSUPPORTED_VERSION for another major version,
UNEXPECTED_MESSAGE otherwise); a frame that fails authentication ends it without a reply. Error
codes NOT_TRUSTED, PAIRING_REJECTED, PEER_BUSY, NOT_PERMITTED, GROUP_FULL, IN_ANOTHER_GROUP and
UNSUPPORTED_VERSION are shown as
such, any other code as a generic peer error. Bye 3 (going to sleep) ends the session with
PEER_SLEEPING, so the peer is expected back; Bye 6 (revoked) with NOT_TRUSTED; Bye 7 without an
error; every other reason with CONNECTION_CLOSED.

| Timing | Value |
|--------|-------|
| Hello through handshake message 3 | 10 s |
| The user's answer to a pairing code | 120 s |
| Heartbeat after sending nothing for | 250 ms |
| Peer reported unresponsive, and input to and from it stopped, after silence of | 750 ms |
| Session closed with TIMED_OUT after silence of | 10 s |
| Ping (round trip and clock offset) | every 1 s |
| First complete frame on an accepted connection | 10 s |

### Input datagrams

Once the peer's SessionSetup arrives, a side sends InputHello with the peer's `input_token` to the
peer's `udp_port` at the address of the control connection: every 250 ms until the peer's
InputHello arrives, then every 5 s. A valid InputHello tells the receiver where to send input
datagrams, so it also follows the peer to a new address. PointerMotion is accepted only in an
established session and only when its sequence is higher than that of the last one applied.

### Transfer streams

The side that receives data opens the stream: a TCP connection to the peer's `listen_port` at the
address of the control connection, whose first frame is TransferHello with the token from the
peer's SessionSetup and a stream ID of its own parity. The accepting side closes the connection
unless the token belongs to an established session, the parity is the opener's and the ID was not
used before in that session. Ending a session ends its streams. A side accepts no more messages
for a stream while 4 MiB are waiting to be written to it, and continues once less than 1 MiB is.

### Transfers

- **Order.** The receiving side asks for the item's manifest first, then for the content of each
  file and link, whole: offset 0 and length `byte_count` (1 for an empty file). Several requests
  may be outstanding; the sending side answers the requests of one stream in the order they came.
- **Manifests.** Paths are in NFC. When two offered paths have the same name, the sender numbers
  the later ones ("name (2).ext").
- **Content.** ContentEnd's `byte_count` is what was sent for the request and its digest covers
  the whole entry. Before sending it, the sender checks that the file still has the size and
  modification time it had when the item was scanned, and sends TransferError IO_ERROR otherwise.
- **Checks on receipt.** Chunks must continue where the last one ended and stay within the entry's
  `byte_count`; ContentEnd must give that `byte_count` and the digest of what arrived. Anything else
  ends the transfer with INTEGRITY_FAILED (or UNEXPECTED_MESSAGE), and so does any TransferError:
  the receiver removes every file and folder the transfer created.
- **Names on disk.** Receivers map names for their file system as `core` does (DEVPLAN, Phase 13),
  number names one folder would hold twice on a case-insensitive file system, and number top-level
  names the destination already holds.
- **Links.** A receiver resolves a link's target against the link's folder; when it names another
  entry of the same item, the link is created with the target rewritten to the mapped names.
  Absolute targets, targets that leave the item and targets that name nothing in it are skipped.
- **Limits.** A computer serves at most its own `max_transfer_streams` streams from one peer at a
  time; another one gets TransferError PEER_BUSY (request 0) and is closed. Receivers open at most
  the lower of both computers' limits.
- **Ending early.** A receiver that gives up closes the stream. A sender whose object is withdrawn
  answers its outstanding requests with TransferError CANCELLED. TransferCancel drops one request
  and keeps the stream. A receiver may stop reading a stream for a while (its disk is slower); TCP
  then holds the sender back.
- **Who may fetch.** A sender answers requests for items it did not offer to the requesting member
  with NOT_PERMITTED and for unknown items with NOT_FOUND. A receiver fetches nothing from a member
  whose "files" permission it has turned off ("Permissions" below).

## Group membership

### Roster

Every member keeps the group's roster: for each device it knows of, the newest pairing (device ID,
public key, epoch, platform, name) and the newest revoked epoch. A device is a member while its
pairing's epoch is higher than its revoked epoch, so a revocation covers every pairing up to that
epoch and only a new pairing brings the device back.

Merging keeps, per device, the pairing with the higher epoch and the higher revoked epoch. Two
entries for the same epoch carry the same key (the device ID is its fingerprint) and keep the one
whose name, then platform, sorts first. Merges therefore give the same roster in every order, and
a roster from a member that was offline cannot undo a revocation.

### Rosters between members

- A member sends its whole roster to each member whose session is established, and again to every
  connected member whenever its roster changes (except the one the change came from).
- A RosterState is accepted only from a current member and only for this computer's group; the
  group ID is public, so anyone could claim it. The one exception is the computer this one has
  just paired with while in no group: its roster is adopted if it names both of them as members,
  this computer with its own key.
- When a roster names a new member, the others trust it from then on, and those that refused it
  before (NOT_TRUSTED, because they had not heard of it) dial it again.
- When a change makes a connected peer a non-member, its session ends with Bye 6 (revoked). A
  computer that finds itself revoked in a roster, or receives Bye 6 from a member, leaves the
  group and reports NOT_TRUSTED.

### Pairing

After the handshake, each side decides about the peer in this order:

| The peer | Decision |
|----------|----------|
| A member | Trusted: PairingConfirm *accepted* at once |
| In this computer's group, but not a member: not in its roster yet, or revoked without having heard of it | Error NOT_TRUSTED; dialled again once a roster names it |
| In another group, while this computer is in one | Error IN_ANOTHER_GROUP |
| Anyone else while a pairing code is shown here | Error PEER_BUSY |
| In no group, while this computer's group has 8 members | Error GROUP_FULL |
| Anyone else | Pairing code |

A revoked device that has left the group (it does when it learns of its revocation) is in no
group, so it gets a pairing code like any other computer, from whichever side starts the pairing.

Once both users have accepted:

- A computer in a group adds the other with an epoch higher than any it knows for that device and
  sends the roster to it and to every connected member.
- When neither is in a group, the one with the lower device ID creates one (a random group ID,
  both computers as members); the other waits for its roster.
- The computer that joined advertises the group from then on.

A group has at most 8 members. Two members that add computers at the same time can take it past
that; nobody is removed then.

### Leaving

A computer that leaves revokes its own pairing, sends that roster to every connected member, ends
every session with Bye 5 and forgets the group. Members that were offline learn it from the others.

### Permissions

Each computer decides for itself, per member, whether the member may control it, send it files
and share clipboards with it; all three are allowed until the user changes them. A denied
permission clears the matching Capabilities bits (control: bit 0; files: bits 2 and 4; clipboard:
bit 3), and the receiving side refuses requests that ignore that:

| Denied | Refused |
|--------|---------|
| control | Enter gets Refuse 3 (not permitted); KeyEvent, ButtonEvent, ScrollEvent, ReleaseAll and PointerMotion are dropped |
| files | An Enter carrying a drag gets Refuse 3; a forwarded DragOffer (probe_id 0) is dropped, also when its source is a member that may not send files |
| clipboard | ClipboardAnnounce is dropped; a ManifestRequest or ContentRequest for the clipboard gets TransferError NOT_PERMITTED |

## Arrangement

One shared arrangement places every member's logical desktop in a single space; the arrangement
never moves a computer's monitors relative to each other.

### Screens

- A member sends its ScreenReport to each member whose session is established, and to every
  connected member whenever its monitors change. A report replaces the sender's previous one;
  members keep each member's last report, also while it is offline.
- On macOS the frames are the displays in points. On Windows the sender derives them from the
  physical layout: the primary monitor's frame is its physical rectangle scaled by its own scale;
  a monitor that touches an already placed one in physical pixels gets the shared edge in common
  with it and starts as far along that edge as in pixels, scaled by the placed monitor; a monitor
  that touches none keeps its physical position, scaled like the primary.

### Layout document

- LayoutState carries the whole document. A member keeps the newer by (revision, author); for
  the same pair it keeps the one whose placements, listed by device ID as (device_id, x, y,
  detached), sort first, so a faulty member cannot split the group.
- A member sends its document to each member whose session is established, and to every connected
  member whenever its document changes, except the member it came from.
- A member that knows its own screens but is missing from the document adds itself: revision one
  higher, itself as author, placements of computers that are no longer members dropped, and its
  own placement to the right of everything placed: its leftmost monitor's left edge on the
  rightmost monitor's right edge, tops aligned.
- A computer that joins another group, or leaves its group, starts from an empty document.
- An arrangement is valid when no two computers' monitors overlap and the attached computers all
  touch each other, directly or through others. Two monitors touch when facing edges are at most
  8 units apart and share a stretch along them. Editors publish only valid arrangements;
  receivers take any well-formed document, because screens can change after it was made.

## Encryption

### Handshake

Noise Protocol Framework revision 34, pattern **`Noise_XX_25519_ChaChaPoly_BLAKE2b`**, prologue
the 17 ASCII bytes `Osdrop control v1`. The prologue makes a handshake with any other protocol that
uses the same pattern fail, Osplay (empty prologue) among them. The connecting side is the
**initiator**; between two members, the one with the lower device ID connects. The three Noise
messages travel as HandshakeMessage payloads, steps 1–3, after both Hello messages. The
implementation is validated against the published XX vectors of cacophony and noise-c
(`core/tests/data/`).

- Static keys are the devices' identities. A DH result of all zeros (low-order peer key) aborts
  the handshake.
- Handshake payloads are empty in version 1.
- Any failure (authentication, order, size) ends the handshake; it is never resumed.

### Pairing code

```
digest = BLAKE2b-256("Osdrop pairing code v1" || handshake_hash)
code   = (first 8 bytes of digest as u64 little-endian) mod 1000000, shown with leading zeros
```

`handshake_hash` is the final Noise `h` (64 bytes). Any change to the transcript changes the code.

### Session keys

From the final chaining key `ck`, using Noise's HKDF (HMAC-BLAKE2b, two outputs, each truncated to
32 bytes); first output initiator → responder, second responder → initiator:

| Keys | Derivation | Use |
|------|------------|-----|
| control | Noise `Split()` | control channel, as Noise transport messages |
| input | HKDF(ck, "Osdrop input keys v1") | input datagrams |
| transfer base | HKDF(ck, "Osdrop transfer keys v1") | per-stream keys below |

A transfer stream's key for one direction, where `base` is that direction's transfer base key:
`BLAKE2b-256(key = base, message = "Osdrop transfer stream v1" || stream_id (u32 LE))`, keyed
BLAKE2b with a 32-byte key. Frames each side sends on the stream use the key of its own direction
(initiator → responder or responder → initiator), whoever opened the stream. Because stream IDs are
never reused within a session (odd for streams the initiator opens, even for the responder's), no
two streams share a key.

### Control channel and transfer streams after the handshake

Each frame payload is `ChaCha20-Poly1305(key, nonce, ad = empty, message) || tag`; the nonce is the
frame counter of that direction on that connection (0, 1, 2, …) as 32 zero bits followed by the
64-bit counter little-endian. A replayed, reordered or altered frame fails authentication and ends
the connection. Counter 2^64−1 is reserved by Noise; reaching it ends the connection
(`NONCE_EXHAUSTED`).

### Input datagrams

```
datagram = input header (12 bytes, clear)
         | ChaCha20-Poly1305(input key of the sender's direction,
                             nonce = 32 zero bits || sequence (u64 LE),
                             ad = input header, plaintext = input body)
         | tag (16 bytes)
```

A sender never repeats a sequence number. A receiver keeps a sliding window of the last 1024
sequence numbers and drops datagrams older than the window or already seen (`REPLAYED`).

## Error codes

Decoding and encoding report these codes, which are also the values of `osdrop_result` in
`core/include/osdrop/osdrop.h` (values never change; new codes are appended):

| Code | Name | Meaning |
|-----:|------|---------|
| 0 | OK | |
| 1 | INVALID_ARGUMENT | null pointer or unknown enum passed to the API |
| 2 | BUFFER_TOO_SMALL | the output buffer cannot hold the result |
| 3 | TRUNCATED | input ends before a field |
| 4 | BAD_MAGIC | the first two bytes are not "OD" |
| 5 | UNSUPPORTED_VERSION | the version byte is not 1 |
| 6 | UNKNOWN_TYPE | unknown message or datagram type |
| 7 | TRAILING_BYTES | input continues after the last field |
| 8 | VALUE_OUT_OF_RANGE | a field violates its documented range, a reserved field is not 0, or a count or uniqueness rule is broken |
| 9 | INVALID_STRING | a string, type ID, name or path breaks its rules |
| 10 | TOO_LARGE | input is larger than the largest valid message or datagram |
| 11 | DECRYPTION_FAILED | authentication failed: wrong key, altered or forged data |
| 12 | HANDSHAKE_OUT_OF_ORDER | a handshake step was used out of order, or after a failure |
| 13 | REPLAYED | an input datagram was already received or is older than the replay window |
| 14 | NONCE_EXHAUSTED | a cipher reached its last nonce |
| 15 | INVALID_KEY | an all-zero secret key, or a peer key producing an all-zero shared secret |
| 16 | RANDOM_UNAVAILABLE | the platform's random source failed |
| 17 | TIMED_OUT | the handshake or the peer stopped responding |
| 18 | CONNECTION_CLOSED | the peer ended the session (Bye) or the connection closed |
| 19 | PEER_ERROR | the peer sent an Error message |
| 20 | UNEXPECTED_MESSAGE | a valid message arrived in a state where it is not allowed |
| 21 | NOT_TRUSTED | the peer is not a member of this computer's group |
| 22 | PAIRING_REJECTED | a user rejected the pairing code |
| 23 | PEER_BUSY | the peer refused because it is busy |
| 24 | INVALID_STATE | the command is not valid in the current state |
| 25 | NETWORK | a socket could not be opened, connected or used |
| 26 | INTERNAL | the core could not allocate memory or start its I/O thread |
| 27 | PEER_SLEEPING | the peer closed the session with Bye 3 (going to sleep); it is expected back |
| 28 | NOT_PERMITTED | this computer's local permissions refuse the request from this peer |
| 29 | GROUP_FULL | pairing refused: the group already has the most computers allowed (8) |
| 30 | IN_ANOTHER_GROUP | pairing refused: the two computers belong to different groups; one must leave its group first |
| 31 | INVALID_LAYOUT | an arrangement was refused: monitors of two computers overlap, or a computer touches no other and is not detached |
| 32 | CANCELLED | a transfer was cancelled on either side |
| 33 | IO_ERROR | a file could not be read or written |
| 34 | NO_SPACE | the receiver has not enough free space for a transfer |
| 35 | INTEGRITY_FAILED | received content does not match the sender's digest |
| 36 | NOT_FOUND | the requested item, entry or file no longer exists |

## Fixtures

`protocol/fixtures/manifest.tsv` lists every fixture: file, kind (`control`, `input` or
`transfer`), expected outcome (`valid` with the message type, or `error` with the error code) and a
description. `protocol/tools/generate_fixtures.py` writes the binary files; CI fails if they differ
from what the generator produces. The C++ and Swift test suites both read the manifest and must
reach the same outcome for every file; valid fixtures must also re-encode to identical bytes.
Discovery TXT records are covered by unit tests, not fixtures.
