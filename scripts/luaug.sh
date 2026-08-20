#!/usr/bin/env bash
# `luaug`, as a developer types it.
#
# The CLI is Lute scripts run by the pinned `lute` rather than a compiled binary
# (M3 brief, Decision 2). This wrapper is what makes the documented command line
# exist without a build step in front of every gate run.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# rokit installs its shims here and does not add them to PATH for a
# non-interactive shell, which is why a local run would otherwise fail with
# "lute: command not found" while CI is perfectly happy.
if [[ -d "$HOME/.rokit/bin" ]]; then
    PATH="$HOME/.rokit/bin:$PATH"
    export PATH
fi

exec lute run "$repo/tools/cli/main.luau" -- "$@"
