#!/usr/bin/env python3
"""Runs the pinned clang-tidy on every first-party, non-test source in a compilation database.

Usage: run_clang_tidy.py <build-dir> <path-prefix>... [-- <extra clang-tidy args>]
The clang-tidy version is read from scripts/lint.sh so there is a single pin.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def pinned_version() -> str:
    match = re.search(r'^readonly CLANG_TIDY_VERSION="(.+)"$', (ROOT / "scripts/lint.sh").read_text(), re.M)
    if not match:
        sys.exit("CLANG_TIDY_VERSION not found in scripts/lint.sh")
    return match.group(1)


def sources(build_dir: Path, prefixes: list[str]) -> list[str]:
    entries = json.loads((build_dir / "compile_commands.json").read_text())
    found = set()
    for entry in entries:
        path = Path(entry["directory"], entry["file"]).resolve()
        try:
            relative = path.relative_to(ROOT).as_posix()
        except ValueError:
            continue  # fetched dependencies outside the repository
        if "/build/" in f"/{relative}" or "/tests/" in relative or "/_deps/" in relative:
            continue
        if any(relative.startswith(prefix) for prefix in prefixes):
            found.add(relative)
    return sorted(found)


def main() -> int:
    args = sys.argv[1:]
    extra: list[str] = []
    if "--" in args:
        split = args.index("--")
        args, extra = args[:split], args[split + 1 :]
    if len(args) < 2:
        sys.exit(__doc__)
    build_dir = (ROOT / args[0]).resolve()
    files = sources(build_dir, args[1:])
    if not files:
        sys.exit(f"no sources matched {args[1:]} in {build_dir}")
    command = ["uvx", f"clang-tidy@{pinned_version()}", "-p", str(build_dir), "--quiet", *extra, *files]
    return subprocess.call(command, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main())
