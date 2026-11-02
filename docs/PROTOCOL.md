# Osdrop wire protocol

> Status: not yet specified. Phase 5 replaces this page with the authoritative, field-by-field
> specification and the binary fixtures in `protocol/fixtures/`. Until then the fixed decisions
> are in [`DEVPLAN.md`](../DEVPLAN.md) → Protocol Requirements.

## Fixed decisions

- All integers little-endian. Every message, datagram and transfer frame starts with a 2-byte magic
  and a 1-byte protocol version.
- Discovery: Bonjour / DNS-SD service type `_osdrop._tcp`.
- Control channel: TCP, 4-byte length prefix, encrypted after the Noise XX handshake. Keys,
  buttons and scroll travel here with the cursor position they belong to.
- Input datagrams: UDP, pointer motion only, latest wins, AEAD-encrypted with a replay window.
- Transfer streams: separate TCP connections for file data, ranged reads, per-file hashes.
- Hand-written fixed binary layouts; no protobuf or JSON on the wire.
