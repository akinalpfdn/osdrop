# Phase 006 (MVP-06) — Protocol codec and fixtures
Status: PENDING

DEVPLAN reference: Phase 5. Depends on: Phase 0

## Goal
The complete wire format exists as a pure, tested library and a written specification.

## Tasks
- [ ] `docs/PROTOCOL.md`: every control message, input datagram, transfer frame and the discovery TXT record, field by field.
- [ ] `core/protocol`: encode/decode with strict bounds checking.
- [ ] Fixtures in `protocol/fixtures/` (valid and malformed) from the independent Python generator.
- [ ] C++ and Swift (through `CoreBridge`) suites against the same fixtures; fuzz targets for every decoder.
- [ ] `capi` for version information and error codes.

## Acceptance Criteria
- [ ] Every message round-trips; every malformed fixture fails with its documented error and never crashes.
- [ ] Regenerated fixtures are byte-identical in CI.
- [ ] Fuzz targets run in CI with a time budget and no findings.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
