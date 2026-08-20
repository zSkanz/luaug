#!/usr/bin/env bash
# The Luau-side static gates: formatting, strict analysis under the new type
# solver (R2), the i18n lint (R3), the IDL's own naming lints, generated-file
# freshness, and module layering (architecture.md §2).
#
# This file is the gate. `.github/workflows/ci.yml` runs it and so does
# `scripts/localgate.ps1` -- the same script, not a transcription, because a
# transcription drifts until "it passes locally" stops meaning anything.
#
# The first two checks are `luaug check` (M3 brief, Decision 7): the CLI is the
# implementation and this calls it, so a developer typing `luaug check` in a
# scaffolded project runs what the gate runs. Everything after it is
# repository-specific -- the IDL, its generated outputs, the module layering --
# and has no meaning inside a user's game.
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

echo "== luaug check (analysis + formatting) =="
# Through `bash` rather than by executing it. The executable bit is not tracked
# by a Git working tree on Windows unless `core.fileMode` says so, and this
# script was committed without it -- which is a red CI and a green local run,
# the exact split "run the gates locally" is supposed to prevent. The bit is set
# in the index now as well; this is the half that cannot be lost again.
bash scripts/luaug.sh check . --definitions=runtime/types/engine.d.luau

# The IDL's own lints: casing, the Async biconditional, event tense, enum
# singularity (api-design.md §9). These are the rules a type cannot express, and
# they gate rather than report -- §2.5's rename list is frozen, so a bad name
# that reaches a release is a name the engine keeps forever.
echo "== api definition lints (api-design §9) =="
lute api/generator/check.luau

# The generated definitions are checked in, so drift has to be a build failure
# rather than a discovery.
#
# The comparison is against the file as it was BEFORE regenerating, not against
# git. Regenerating first and then diffing the working tree looks equivalent and
# is not: it overwrites a hand edit and then reports that nothing is wrong, so
# the one failure mode a generated file actually has -- somebody editing it --
# is the one it cannot see. Verified by trying it.
echo "== generated type definitions are fresh =="
defs_before="$(mktemp)"
trap 'rm -f "$defs_before"' EXIT
cp runtime/types/engine.d.luau "$defs_before"
lute api/generator/gen_dts.luau >/dev/null
if ! diff -q "$defs_before" runtime/types/engine.d.luau >/dev/null; then
    echo "luau-check: runtime/types/engine.d.luau does not match the IDL." >&2
    echo "  Either the definitions were hand-edited, or api/defs changed without" >&2
    echo "  regenerating. Both are the same fix: commit the regenerated file." >&2
    diff -u "$defs_before" runtime/types/engine.d.luau | head -40 >&2
    exit 1
fi

# The C++ reflection tables are checked in for the same reason, and compared the
# same way round: against copies taken BEFORE the generator runs.
echo "== generated class descriptors are fresh =="
header_before="$(mktemp)"
source_before="$(mktemp)"
trap 'rm -f "$defs_before" "$header_before" "$source_before"' EXIT
cp engine/scene/generated/class_descriptors.gen.h "$header_before"
cp engine/scene/generated/class_descriptors.gen.cpp "$source_before"
lute api/generator/gen_cpp.luau >/dev/null
for pair in "$header_before:engine/scene/generated/class_descriptors.gen.h" \
            "$source_before:engine/scene/generated/class_descriptors.gen.cpp"; do
    before="${pair%%:*}"
    after="${pair#*:}"
    if ! diff -q "$before" "$after" >/dev/null; then
        echo "luau-check: $after does not match the IDL." >&2
        echo "  Either the descriptors were hand-edited, or api/defs changed without" >&2
        echo "  regenerating. Both are the same fix: commit the regenerated file." >&2
        diff -u "$before" "$after" | head -40 >&2
        exit 1
    fi
done

# The api-dump is the artifact api-design.md §5 keeps for a different reason
# from the other two: not so a build can consume it, but so that a change to the
# public surface CANNOT land without appearing in a reviewable diff -- "to force
# changelog entries and catch accidental API breaks". Same comparison, taken the
# same way round, for the same reason.
echo "== the api dump matches the IDL =="
dump_before="$(mktemp)"
trap 'rm -f "$defs_before" "$header_before" "$source_before" "$dump_before"' EXIT
cp api/api-dump.json "$dump_before"
lute api/generator/gen_dump.luau >/dev/null
if ! diff -q "$dump_before" api/api-dump.json >/dev/null; then
    echo "luau-check: api/api-dump.json does not match the IDL." >&2
    echo "  The public surface changed. Commit the regenerated dump alongside the" >&2
    echo "  definition change, so the diff is on the record -- that is the whole" >&2
    echo "  job of this file (api-design.md section 5)." >&2
    diff -u "$dump_before" api/api-dump.json | head -40 >&2
    exit 1
fi

echo "== i18n keys and hardcoded strings (R3) =="
lute tools/repo/i18nlint.luau

echo "== module layering =="
lute tools/repo/checklayers.luau

echo "== the CLI's own tests =="
lute test tools/cli/tests

echo "luau-check: ok"
