#!/usr/bin/env python3
"""Runs the pinned clang-tidy on every first-party, non-test source in a compilation database.

Usage: run_clang_tidy.py <build-dir> <path-prefix>... [-- <extra clang-tidy args>]
The clang-tidy version is read from scripts/lint.sh so there is a single pin.

Vendored code (any `third_party/` directory) is exempt from linting. Header filtering keeps its
ordinary diagnostics out, but the static analyzer can still report inside it when a path starts in
first-party code (Asio's Windows thread start-up, under MSVC's headers); such reports are printed
and do not fail the run.
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


DIAGNOSTIC = re.compile(r"^(?P<path>.+?):\d+:\d+: (?:error|warning): ")


def blocking(output: str) -> tuple[int, int]:
    """(diagnostics in first-party files, diagnostics in vendored files) found in `output`."""
    first_party = vendored = 0
    for line in output.splitlines():
        match = DIAGNOSTIC.match(line)
        if not match:
            continue
        if "/third_party/" in match.group("path").replace("\\", "/"):
            vendored += 1
        else:
            first_party += 1
    return first_party, vendored


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
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode == 0:
        return 0
    first_party, vendored = blocking(result.stdout + result.stderr)
    if vendored > 0 and first_party == 0:
        print(f"Ignored {vendored} diagnostic(s) in vendored third-party code.", file=sys.stderr)
        return 0
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
