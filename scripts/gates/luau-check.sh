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

# The editor's icon ids, same shape and same reason: `icons/default/theme.json`
# is the list of ids that exist, and a call site naming one by hand is a typo
# that becomes a blank square in a panel nobody happened to open.
echo "== generated icon ids are fresh =="
icons_before="$(mktemp)"
trap 'rm -f "$defs_before" "$icons_before"' EXIT
cp engine/app/generated/icon_ids.gen.h "$icons_before"
lute tools/repo/genicons.luau >/dev/null
if ! diff -q "$icons_before" engine/app/generated/icon_ids.gen.h >/dev/null; then
    echo "luau-check: engine/app/generated/icon_ids.gen.h does not match the theme." >&2
    echo "  Either it was hand-edited, or icons/default/theme.json changed without" >&2
    echo "  regenerating. Both are the same fix: commit the regenerated file." >&2
    diff -u "$icons_before" engine/app/generated/icon_ids.gen.h | head -40 >&2
    exit 1
fi

# The C++ reflection tables are checked in for the same reason, and compared the
# same way round: against copies taken BEFORE the generator runs.
#
# gen_cpp.luau emits one pair per engine module that registers classes into
# scene's registry (architecture.md §2, rule 3), so the set is DISCOVERED rather
# than listed here: a gate that names the files it checks is a gate that stops
# covering the next module, and stops silently. `sort` under LC_ALL=C so the
# manifest is the same on every platform.
echo "== generated class descriptors are fresh =="
descriptors_before="$(mktemp -d)"
trap 'rm -f "$defs_before" "$icons_before"; rm -rf "$descriptors_before"' EXIT

descriptor_files() {
    find engine -type f -path 'engine/*/generated/class_descriptors.gen.*' | LC_ALL=C sort
}

descriptor_files >"$descriptors_before/manifest"
while read -r file; do
    mkdir -p "$descriptors_before/$(dirname "$file")"
    cp "$file" "$descriptors_before/$file"
done <"$descriptors_before/manifest"

lute api/generator/gen_cpp.luau >/dev/null

# A file that appeared or vanished is drift too: a new module's output is not
# checked in until this says so, and a stale one left behind after a module is
# removed would otherwise sit in the tree being compiled.
if ! descriptor_files | diff -q - "$descriptors_before/manifest" >/dev/null; then
    echo "luau-check: the set of generated descriptor files changed." >&2
    echo "  gen_cpp.luau emits one pair per module in its Modules list. Commit the" >&2
    echo "  files it now writes, and delete the ones it no longer does." >&2
    descriptor_files | diff -u "$descriptors_before/manifest" - >&2 || true
    exit 1
fi

while read -r file; do
    if ! diff -q "$descriptors_before/$file" "$file" >/dev/null; then
        echo "luau-check: $file does not match the IDL." >&2
        echo "  Either the descriptors were hand-edited, or api/defs changed without" >&2
        echo "  regenerating. Both are the same fix: commit the regenerated file." >&2
        diff -u "$descriptors_before/$file" "$file" | head -40 >&2
        exit 1
    fi
done <"$descriptors_before/manifest"

# The api-dump is the artifact api-design.md §5 keeps for a different reason
# from the other two: not so a build can consume it, but so that a change to the
# public surface CANNOT land without appearing in a reviewable diff -- "to force
# changelog entries and catch accidental API breaks". Same comparison, taken the
# same way round, for the same reason.
echo "== the api dump matches the IDL =="
dump_before="$(mktemp)"
trap 'rm -f "$defs_before" "$icons_before" "$dump_before"; rm -rf "$descriptors_before"' EXIT
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

# The vendored trees agree with the manifest, and -- the part that needs a gate
# rather than a habit -- a NARROWED row's include list still covers everything
# this repository's build files name inside it (ADR 0042, approved 2026-08-21 on
# exactly this condition).
#
# A hand-curated include list rots: at the next pin bump upstream moves a file
# out of the listed paths, and the build either breaks obscurely or quietly stops
# compiling something. Narrowing without verification is debt with interest.
echo "== vendored trees and narrowed include lists (ADR 0042) =="
lute tools/repo/vendor.luau status

# Every component field has a reader, or its property is marked `Inert`. The
# mechanical half of D030: `Inert` was built at M4.5 so that "stored and nothing
# acts on it" would be visible, and two milestones later a property was born into
# exactly that state and nobody marked it. A marker that depends on somebody
# remembering is the one thing it exists not to depend on.
echo "== stored-and-unread properties =="
lute tools/repo/inertcheck.luau

# The API reference is generated from the same IDL and checked in like every
# other generated artifact (roadmap M8). Compared as a DIRECTORY rather than as a
# file list, because a class removed from the IDL has to take its page with it --
# and a stale page is exactly the documentation that outlives what it describes.
echo "== generated API reference is fresh =="
reference_before="$(mktemp -d)"
trap 'rm -f "$defs_before" "$icons_before" "$dump_before"; rm -rf "$reference_before" "$descriptors_before"' EXIT
cp -r docs/api/. "$reference_before"/
lute api/generator/gen_reference.luau >/dev/null
if ! diff -r -q "$reference_before" docs/api >/dev/null; then
    echo "luau-check: docs/api does not match the IDL." >&2
    echo "  Either a page was hand-edited, or api/defs changed without" >&2
    echo "  regenerating. Both are the same fix: commit the regenerated pages." >&2
    diff -r -u "$reference_before" docs/api | head -40 >&2
    exit 1
fi

# R6, as a check rather than as an afternoon. M8's scope asks for a licence and
# NOTICE audit of every vendored dependency; an audit somebody performs once is a
# fact about one day, and a pin bump is exactly when it stops being true.
echo "== licences (R6) =="
lute tools/repo/licensecheck.luau

echo "== the CLI's own tests =="
lute test tools/cli/tests

echo "luau-check: ok"
