#!/usr/bin/env bash
# Git Bash entry point on Windows; never deploys into a game installation.
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v cmd.exe >/dev/null; then
    echo "Windows/MSVC required; use GitHub Actions. No Linux DLL build is performed." >&2
    exit 1
fi
cmd.exe /c build_vs.bat
