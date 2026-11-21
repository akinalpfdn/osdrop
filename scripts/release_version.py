#!/usr/bin/env python3
"""Prints the release version after checking that every place holding it agrees.

The Windows app's version is <Version> in windows/app/Directory.Build.props, the Mac app's is
MARKETING_VERSION in macos/project.yml. With --tag, the release tag (v<version>) must match too.
The release workflow and scripts/macos/release.sh both call it, so the two downloads of a release
always carry the same version.

Usage: release_version.py [--tag v0.1.0]
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS_PROPS = ROOT / "windows/app/Directory.Build.props"
MAC_PROJECT = ROOT / "macos/project.yml"
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
MARKETING_VERSION = re.compile(r'^\s*MARKETING_VERSION:\s*"?([^"\s]+)"?\s*$', re.MULTILINE)


def windows_version() -> str:
    element = ET.parse(WINDOWS_PROPS).getroot().find("./PropertyGroup/Version")
    return (element.text or "").strip() if element is not None else ""


def mac_version() -> str:
    match = MARKETING_VERSION.search(MAC_PROJECT.read_text(encoding="utf-8"))
    return match.group(1) if match else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", help="the release tag, which must be v<version>")
    arguments = parser.parse_args()

    windows, mac = windows_version(), mac_version()
    problems = []
    if not VERSION.match(windows):
        problems.append(f"{WINDOWS_PROPS.relative_to(ROOT)}: <Version> '{windows}' is not X.Y.Z")
    if not VERSION.match(mac):
        problems.append(f"{MAC_PROJECT.relative_to(ROOT)}: MARKETING_VERSION '{mac}' is not X.Y.Z")
    if windows != mac:
        problems.append(f"the Windows app is {windows} but the Mac app is {mac}")
    if arguments.tag is not None and arguments.tag != f"v{windows}":
        problems.append(f"the tag {arguments.tag} does not match the apps' version v{windows}")
    if problems:
        print("\n".join(f"error: {problem}" for problem in problems), file=sys.stderr)
        return 1
    print(windows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
