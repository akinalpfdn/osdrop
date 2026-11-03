#!/usr/bin/env python3
"""Extracts the Noise_XX_25519_ChaChaPoly_BLAKE2b vectors from published Noise test-vector files.

Usage: extract_noise_vectors.py <cacophony.txt> <noise-c-basic.txt> > noise_xx_vectors.txt

Sources (pinned):
  cacophony  https://github.com/haskell-cryptography/cacophony/blob/8ee9d41e34a1a596cfa3ab12aa4069ff87dc1247/vectors/cacophony.txt (Unlicense)
  noise-c    https://github.com/rweather/noise-c/blob/cfe25410979a87391bb9ac8d4d4bef64e9f268c6/tests/vector/noise-c-basic.txt (MIT)
"""

from __future__ import annotations

import json
import sys

PROTOCOL = "Noise_XX_25519_ChaChaPoly_BLAKE2b"


def hex_or_dash(value: str) -> str:
    return value if value else "-"


def main() -> int:
    print(f"# {PROTOCOL} test vectors, extracted by core/tests/data/extract_noise_vectors.py.")
    print("# Format: one 'key value' per line; 'message <payload> <ciphertext>' in order; '-' is empty.")
    for label, path in (("cacophony", sys.argv[1]), ("noise-c", sys.argv[2])):
        vectors = [v for v in json.load(open(path))["vectors"] if v.get("protocol_name", v.get("name")) == PROTOCOL]
        for vector in vectors:
            assert vector["init_prologue"] == vector["resp_prologue"]
            print(f"vector {label}")
            for key in ("init_prologue", "init_static", "init_ephemeral", "resp_static", "resp_ephemeral",
                        "handshake_hash"):
                print(f"{key} {hex_or_dash(vector[key])}")
            for message in vector["messages"]:
                print(f"message {hex_or_dash(message['payload'])} {hex_or_dash(message['ciphertext'])}")
            print("end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
