# Protocol fixtures

`fixtures/` holds binary samples of every control message, input datagram and transfer frame, both
valid and malformed. They are written by an independent Python encoder in `tools/`, and the C++
(GoogleTest) and Swift test suites decode the same files, so both apps are held to one wire format.
The format itself is specified in [`docs/PROTOCOL.md`](../docs/PROTOCOL.md).

Fixtures are added in Phase 5. Files here are treated as binary by Git (`.gitattributes`).
