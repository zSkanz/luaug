#!/usr/bin/env bash
# Build the documentation site and open it. See scripts/docs.ps1 for why this is
# a build product rather than a checked-in artifact.
#
# --out=DIR writes somewhere else; --no-open builds without opening.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ -d "$HOME/.rokit/bin" ]]; then
    PATH="$HOME/.rokit/bin:$PATH"
    export PATH
fi

out=""
open=1
for argument in "$@"; do
    case "$argument" in
        --out=*) out="${argument#--out=}" ;;
        --no-open) open=0 ;;
        *)
            echo "docs: unknown option $argument" >&2
            exit 2
            ;;
    esac
done

if [[ -n "$out" ]]; then
    lute api/generator/gen_site.luau "--out=$out"
    target="$out"
else
    lute api/generator/gen_site.luau
    if [[ -n "${LUAUG_BUILD_ROOT:-}" ]]; then
        target="${LUAUG_BUILD_ROOT//\\//}/docs-site"
    else
        target="out/docs-site"
    fi
fi

index="$target/index.html"
if [[ ! -f "$index" ]]; then
    echo "docs: the generator reported success and wrote no index at $index" >&2
    exit 1
fi

if [[ $open -eq 1 ]]; then
    # Whichever of these exists; a machine with none of them still gets the path.
    if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$index" >/dev/null 2>&1 || true
    elif command -v open >/dev/null 2>&1; then
        open "$index" || true
    elif command -v start >/dev/null 2>&1; then
        start "$index" || true
    fi
fi

echo "docs: $index"
