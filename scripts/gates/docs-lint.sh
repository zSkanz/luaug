#!/usr/bin/env bash
# The documentation gate: relative links resolve, pinned versions are named
# consistently, no stray Roblox references, the ledger keeps its shape.
#
# This file is the gate. `.github/workflows/ci.yml` runs it and so does
# `scripts/localgate.ps1`, which is the point: the logic used to live only in
# the workflow, so running it locally meant copying commands, and a copy drifts
# until the two disagree about whether the repository is green.
#
# Run from the repository root. Exits non-zero on the first category that fails,
# after reporting every violation in it.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

# Annotations when GitHub is listening, plain text when a human is -- but the
# path is printed either way. A first draft dropped it locally, on the reasoning
# that `file=` was "the annotation", and produced two identical FAIL lines that
# named nothing.
err() { # message, [file]
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error ${2:+file=$2}::$1"
    fi
    echo "  FAIL: ${2:+$2: }$1" >&2
}

status=0

# --- Relative links resolve -------------------------------------------------
# NOTE ON `--exclude-dir=third_party` THROUGHOUT: every check here is about
# documentation *we* authored. Vendored upstream trees are governed by ADR 0021
# (pinned, never edited in place, R13), carry their own licenses, and
# legitimately contain both broken relative links and the word "Roblox" (Luau is
# a Roblox project). Linting them would be meaningless and a permanent source of
# false failures.
echo "== relative links =="
while IFS=: read -r file link; do
    target="${link%%#*}"
    [[ -z "$target" ]] && continue
    base="$(dirname "$file")"
    if [[ ! -e "$base/$target" && ! -e "$target" ]]; then
        err "broken relative link: $link" "$file"
        status=1
    fi
done < <(grep -RnoE '\]\(([^)#h][^):]*)\)' --include='*.md' \
    --exclude-dir=third_party . |
    sed -E 's/^([^:]+):[0-9]+:\]\(([^)]*)\)/\1:\2/')

# --- Pinned versions are named identically everywhere -----------------------
echo "== version consistency =="
check_version() { # expected, forbidden-pattern, label
    if grep -RnE "$2" --include='*.md' --include='*.toml' --include='*.json' \
        --exclude-dir=.git --exclude-dir=third_party .; then
        err "version inconsistency for $3 (found '$2'; expected only '$1')"
        status=1
    fi
}
check_version "0.734" "pinned to 0\.7(0|1|2)[0-9]" "Luau"
check_version "lute@1.0.0" "lute@0\." "Lute"
check_version "luau-lsp@1.69.0" "luau-lsp@1\.6[0-8]\." "luau-lsp"

# --- Legal sweep (R7) -------------------------------------------------------
# `.vscode/settings.json` is allowed for the reason the rule exists: it is the
# file that points luau-lsp AWAY from Roblox's platform mode, and the comment
# explaining why cites ADR 0020. A rule that forbade naming the thing being
# avoided would push that reasoning out of the file where the next person needs
# it -- and that person would then "fix" the setting by reverting it.
echo "== legal sweep (R7) =="
# `scripts/gates/` is allowed for the same reason `.vscode/settings.json` is:
# these files implement the sweep, so the comment explaining what it forbids has
# to name it. The rule aims at content the project publishes, not at the code
# that enforces the rule.
allowed='^(\./)?(README\.md|CONTRIBUTING\.md|NOTICE|MASTER_PROMPT\.md|CLAUDE\.md|PROGRESS\.md|docs/|templates/README\.md|examples/README\.md|tests/README\.md|runtime/README\.md|api/README\.md|engine/README\.md|tools/README\.md|third_party/README\.md|\.vscode/settings\.json|scripts/gates/)'
while IFS= read -r f; do
    if ! [[ "$f" =~ $allowed ]]; then
        err "'Roblox' referenced outside the allowed docs set (rule R7)" "$f"
        status=1
    fi
    # Tracked files only. R7 is about what this repository publishes, and a
    # working tree also holds things it does not: a developer's local editor
    # state, a scratch file, an ignored tool config. Sweeping those made the
    # gate depend on whose machine it ran on -- `.claude/settings.local.json`,
    # which is globally gitignored, turned it red on this one.
done < <(git ls-files -z -- . ':(exclude)third_party' ':(exclude).github' \
    | xargs -0 grep -liE 'roblox' 2>/dev/null || true)

# --- Ledger shape (MASTER_PROMPT.md §11) ------------------------------------
echo "== ledger format =="
for heading in "## State" "## Now / Next" "## Blocked" "## Session Log"; do
    if ! grep -q "$heading" PROGRESS.md; then
        err "PROGRESS.md is missing section '$heading'"
        status=1
    fi
done

# --- Every example carries its launcher (examples/README.md) ----------------
#
# The README states this as a convention and a convention in prose is a
# convention the next example forgets -- which is exactly what happened when
# `02-meshes` shipped without one. A launcher nothing depends on still has to
# exist, because its whole job is that a human does not have to remember where
# an out-of-tree build put the binary (R14).
echo "== every example has a launcher =="
for example in examples/*/; do
    [[ -f "$example/README.md" || -d "$example/src" || -f "$example/init.luau" ]] || continue
    if [[ ! -f "$example/run.bat" ]]; then
        err "$example has no run.bat (examples/README.md: every example folder carries one)"
        status=1
    fi
done

if [[ $status -eq 0 ]]; then
    echo "docs-lint: ok"
fi
exit $status
