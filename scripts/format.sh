#!/usr/bin/env bash
# Applies clang-format and swift-format in place, using the versions and paths pinned in lint.sh.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

clang_format_version="$(sed -n 's/^readonly CLANG_FORMAT_VERSION="\(.*\)"$/\1/p' scripts/lint.sh)"
read -r -a swift_sources <<<"$(sed -n 's/^readonly SWIFT_SOURCES=(\(.*\))$/\1/p' scripts/lint.sh)"

git ls-files --cached --others --exclude-standard -- '*.h' '*.hpp' '*.c' '*.cpp' ':!:*/third_party/*' |
    xargs uvx "clang-format@${clang_format_version}" -i
xcrun swift-format format --in-place --recursive --parallel "${swift_sources[@]}"
