# Protocol fixtures

`fixtures/` holds binary samples of every control message, input datagram and transfer frame, both
valid and malformed. They are written by an independent Python encoder in `tools/`, and the C++
(GoogleTest) and Swift test suites decode the same files, so both apps are held to one wire format.
The format itself is specified in [`docs/PROTOCOL.md`](../docs/PROTOCOL.md).

`fixtures/manifest.tsv` lists every file with its kind, the outcome a decoder must reach (the
message type, or the error code) and a description. After changing the protocol, regenerate the
files and commit them with the change:

```sh
python3 protocol/tools/generate_fixtures.py          # write fixtures and manifest
python3 protocol/tools/generate_fixtures.py --check  # what scripts/lint.sh and CI run
```

The generator needs only the Python standard library. Files here are treated as binary by Git
(`.gitattributes`).
