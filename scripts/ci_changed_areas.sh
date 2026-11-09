#!/usr/bin/env bash
# Which CI jobs a change needs (.github/workflows/ci.yml): writes core, macos and windows as true
# or false to $GITHUB_OUTPUT. Pushes, and files this cannot place, need everything; documentation
# needs nothing. core covers what both apps build on, so it runs every job.
set -euo pipefail

core=false
macos=false
windows=false
all() { core=true; macos=true; windows=true; }

if [[ "${EVENT:-}" != "pull_request" || -z "${BASE:-}" || -z "${HEAD:-}" ]]; then
    all
else
    while IFS= read -r file; do
        case "$file" in
            core/* | protocol/* | Package.swift | Package.resolved) core=true ;;
            macos/*) macos=true ;;
            windows/* | scripts/windows/*) windows=true ;;
            *.md | docs/* | .claude/* | LICENSE) ;;
            *) all ;; # tooling, CI and anything new
        esac
    done < <(git diff --name-only "$BASE...$HEAD")
fi

echo "core=$core macos=$macos windows=$windows"
{
    echo "core=$core"
    echo "macos=$macos"
    echo "windows=$windows"
} >> "${GITHUB_OUTPUT:-/dev/stdout}"
