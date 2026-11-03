# Phase 006 (MVP-06) — Protocol codec and fixtures
Status: DONE

DEVPLAN reference: Phase 5. Depends on: Phase 0

## Goal
The complete wire format exists as a pure, tested library and a written specification.

## Tasks
- [x] `docs/PROTOCOL.md`: every control message, input datagram, transfer frame and the discovery TXT record, field by field.
- [x] `core/protocol`: encode/decode with strict bounds checking.
- [x] Fixtures in `protocol/fixtures/` (valid and malformed) from the independent Python generator.
- [x] C++ and Swift (through `CoreBridge`) suites against the same fixtures; fuzz targets for every decoder.
- [x] `capi` for version information and error codes.

## Acceptance Criteria
- [x] Every message round-trips; every malformed fixture fails with its documented error and never crashes.
- [x] Regenerated fixtures are byte-identical in CI.
- [x] Fuzz targets run in CI with a time budget and no findings.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Codec scaffolding copied from Osplay `35046970` with provenance.
- Coordinates bounded to ±2^28 so sums of three never overflow.
- Symbolic links are manifest entries whose content is the target; receiver policy in Phase 13.
- One fuzz target per decoder, 30 s each in CI.
