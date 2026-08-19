#!/usr/bin/env bash
# The Luau-side static gates: formatting, strict analysis under the new type
# solver (R2), the i18n key check (R3), and module layering (architecture.md §2).
#
# This file is the gate. `.github/workflows/ci.yml` runs it and so does
# `scripts/localgate.ps1` -- the same script, not a transcription, because a
# transcription drifts until "it passes locally" stops meaning anything.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

# rokit installs its shims here and does not add them to PATH for a
# non-interactive shell, which is why a local run would otherwise fail with
# "lute: command not found" while CI is perfectly happy.
if [[ -d "$HOME/.rokit/bin" ]]; then
    PATH="$HOME/.rokit/bin:$PATH"
    export PATH
fi

for tool in stylua luau-lsp lute; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "luau-check: $tool is not on PATH — run scripts/bootstrap first" >&2
        exit 1
    }
done

# Repo-wide rather than a list of directories: naming directories meant a .luau
# file created anywhere else would silently escape both this and the analyzer.
# third_party is excluded by .styluaignore (vendored source is never
# reformatted -- R13).
echo "== stylua =="
stylua --check .

# --platform=standard: this engine is not Roblox, so no Roblox definitions are
# loaded (ADR 0020, api-design §4).
# --ignore: Lute 1.0.0's own shipped typedefs do not typecheck cleanly under the
# pinned luau-lsp, and gating on them would fail for upstream reasons that have
# nothing to do with our code (M0 finding 2).
echo "== luau-analyze (strict, new solver) =="
# `.d.luau` files declare; they are not sources and analysing them as such
# fails. They are fed in with --definitions instead.
mapfile -t files < <(find . -name '*.luau' -not -name '*.d.luau' -not -path './third_party/*' | sort)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "luau-check: no .luau files found — the analysis gate would pass vacuously" >&2
    exit 1
fi

# The engine's own globals. From M3 these are generated from the IDL into
# runtime/types/ and diff-checked for freshness; until then the M1 preview
# binding carries a hand-written one. Loading it is what lets the example be
# `--!strict` and gated (R2) rather than exempted -- and an exemption is the
# thing that spreads.
definitions=()
definition_count=0
while IFS= read -r defs; do
    definitions+=(--definitions "$defs")
    definition_count=$((definition_count + 1))
done < <(find . -name '*.d.luau' -not -path './third_party/*' | sort)

echo "analyzing ${#files[@]} file(s) with ${definition_count} definition file(s)"
luau-lsp analyze --platform=standard --ignore="**/.lute/**" "${definitions[@]}" "${files[@]}"

# The IDL's own lints: casing, the Async biconditional, event tense, enum
# singularity (api-design.md §9). These are the rules a type cannot express, and
# they gate rather than report -- §2.5's rename list is frozen, so a bad name
# that reaches a release is a name the engine keeps forever.
echo "== api definition lints (api-design §9) =="
lute api/generator/check.luau

# The generated definitions are checked in, so drift has to be a build failure
# rather than a discovery. Regenerate and compare: if this fails, someone either
# hand-edited a generated file or changed the IDL without regenerating, and both
# are the same fix.
echo "== generated type definitions are fresh =="
lute api/generator/gen_dts.luau >/dev/null
if ! git diff --quiet -- runtime/types/; then
    echo "luau-check: runtime/types/ is stale — run 'lute api/generator/gen_dts.luau' and commit the result" >&2
    git --no-pager diff --stat -- runtime/types/ >&2
    exit 1
fi

echo "== i18n keys (R3) =="
lute tools/repo/i18nlint.luau

echo "== module layering =="
lute tools/repo/checklayers.luau

echo "luau-check: ok"
