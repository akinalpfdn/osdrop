#!/usr/bin/env bash
# Runs every formatter in check mode, clang-tidy (core and portable Windows libraries), the UI
# token check, the localization check and the protocol fixture check.
# CI runs this exact script; run it before every commit. Requires: uv, CMake, Ninja, Xcode.
set -euo pipefail

readonly CLANG_FORMAT_VERSION="23.1.3"
readonly CLANG_TIDY_VERSION="22.1.8"
readonly SWIFT_SOURCES=(Package.swift macos/Osdrop macos/Shared macos/OsdropCaptureGuard macos/OsdropTests macos/OsdropHardwareTests macos/CoreBridge macos/CoreBridgeTests)

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

command -v uvx >/dev/null || { echo "error: uv is required (brew install uv)" >&2; exit 1; }

# Tracked and not-yet-committed files, so the check also covers work in progress.
list_files() {
    git ls-files --cached --others --exclude-standard -- "$@"
}

run_on_files() {
    local files=()
    while IFS= read -r file; do
        [[ -f "$file" ]] && files+=("$file")
    done
    if [[ ${#files[@]} -eq 0 ]]; then
        echo "error: no files matched" >&2
        exit 1
    fi
    "$@" "${files[@]}"
}

echo "==> clang-format ${CLANG_FORMAT_VERSION}"
list_files '*.h' '*.hpp' '*.c' '*.cpp' ':!:*/third_party/*' |
    run_on_files uvx "clang-format@${CLANG_FORMAT_VERSION}" --dry-run --Werror

echo "==> swift-format"
xcrun swift-format lint --strict --recursive --parallel "${SWIFT_SOURCES[@]}"

echo "==> UI tokens"
scripts/check_ui_tokens.sh

echo "==> localization"
python3 scripts/check_localization.py

echo "==> protocol fixtures"
python3 protocol/tools/generate_fixtures.py --check

echo "==> Unicode tables"
uvx --python 3.13 python core/tools/generate_unicode_tables.py --check

tidy_extra=()
if [[ "$(uname -s)" == "Darwin" ]]; then
    # The PyPI clang-tidy does not know where Xcode's SDK lives.
    tidy_extra+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
fi

echo "==> clang-tidy ${CLANG_TIDY_VERSION} (core)"
if [[ ! -f core/build/debug/compile_commands.json ]]; then
    (cd core && cmake --preset debug >/dev/null)
fi
python3 scripts/run_clang_tidy.py core/build/debug core/src -- ${tidy_extra[@]+"${tidy_extra[@]}"}

echo "==> clang-tidy ${CLANG_TIDY_VERSION} (portable Windows libraries)"
if [[ ! -f windows/build/portable/compile_commands.json ]]; then
    (cd windows && cmake --preset portable >/dev/null)
fi
python3 scripts/run_clang_tidy.py windows/build/portable windows/shared -- ${tidy_extra[@]+"${tidy_extra[@]}"}

echo "All checks passed."
