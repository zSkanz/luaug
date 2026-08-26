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
# Tracked files only, for the reason the legal sweep below gives in full: a
# working tree also holds what this repository does not publish -- a scratch
# note, a draft, a folder somebody is still assembling -- and sweeping those
# makes the gate depend on whose machine it runs on. It failed on exactly
# that: an untracked `art/` directory with a half-written relative path in it.
# The legal sweep learned this and this check had not.
done < <(git ls-files -z -- '*.md' ':(exclude)third_party' \
    | xargs -0 grep -noE '\]\(([^)#h][^):]*)\)' 2>/dev/null |
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
#
# `api/generator/gen_reference.luau` is allowed for a third version of the same
# reason and is named individually rather than by directory: it GENERATES a page
# in `docs/`, which is allowed, and one of the links on that page points at
# `coming-from-roblox.md`. A generator cannot emit a filename it may not spell.
# `api/generator/site/nav.luau` is allowed for the same reason as
# `gen_reference.luau` and is named individually for the same care: it is the
# documentation site's table of contents, and one of the sections it declares is
# the migration guide. A generator cannot emit a section title it may not spell,
# and moving the title out of the file would only move the problem.
# `art/` is allowed for the first reason as well, and it is the largest case of
# it: the briefs and review notes in that directory state R7 AS A DRAWING
# CONSTRAINT -- "it must resemble no existing engine's mark, and not Roblox,
# which here is a legal line and not a taste one" -- and that sentence is the
# rule doing its job. An artist who cannot be told what not to draw will draw it.
# `branding/README.md` is allowed for the first reason too, and it is the
# clearest case of it in the repository: the sentence that trips this lint is
# the one explaining why the wordmark does NOT split `Luau` from `G`, because
# splitting them would point at the language. That is R7's own reasoning
# written down, and a lint that forbids stating a rule is a lint working
# against it.
allowed='^(\./)?(README\.md|CONTRIBUTING\.md|NOTICE|MASTER_PROMPT\.md|CLAUDE\.md|PROGRESS\.md|docs/|templates/README\.md|examples/README\.md|tests/README\.md|runtime/README\.md|api/README\.md|engine/README\.md|tools/README\.md|third_party/README\.md|branding/README\.md|\.vscode/settings\.json|scripts/gates/|api/generator/gen_reference\.luau|api/generator/site/nav\.luau|art/)'
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

# --- The defect register keeps every row it ever had ------------------------
#
# `docs/defects.md` is append-only, and this is what makes that a fact rather
# than an instruction nobody reads. Three human-reported defects were removed
# from PROGRESS.md -- not archived, removed -- while it was being rewritten to
# close M4, on the day the human was being asked to sign that milestone off.
# A close rewrites the ledger wholesale, and a bullet that disappears leaves
# nothing behind.
#
# A row cannot disappear the same way: the ids have to run D001, D002, ... with
# no holes and no duplicates, so a deleted row leaves a gap this names. It does
# not need git history, which is what lets it run identically in a shallow CI
# clone and on a developer's machine.
echo "== defect register =="
if [[ ! -f docs/defects.md ]]; then
    err "docs/defects.md is missing; it is append-only and never deleted" "docs/defects.md"
    status=1
else
    mapfile -t ids < <(grep -oE '^\| D[0-9]{3} ' docs/defects.md | tr -d '| ' || true)
    if [[ ${#ids[@]} -eq 0 ]]; then
        err "docs/defects.md has no D### rows" "docs/defects.md"
        status=1
    fi
    expected=1
    for id in "${ids[@]}"; do
        want=$(printf 'D%03d' "$expected")
        if [[ "$id" != "$want" ]]; then
            err "defect ids must run in order with no gaps: expected $want, found $id" "docs/defects.md"
            status=1
            break
        fi
        expected=$((expected + 1))
    done

    # Every row says what state it is in, from a closed set. "It is in the
    # table" is not a status, and a row whose state nobody can read is a row
    # that gets skipped when someone asks what is still open.
    while IFS= read -r line; do
        state=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $5); print $5}')
        case "$state" in
            open|fixed|not-a-defect|scheduled|quarantined) ;;
            *)
                id=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $2); print $2}')
                err "$id has state '$state'; expected open|fixed|not-a-defect|scheduled|quarantined" "docs/defects.md"
                status=1
                ;;
        esac
    done < <(grep -E '^\| D[0-9]{3} ' docs/defects.md || true)

    # A defect referred to anywhere else has to exist here. This is the half
    # that catches the opposite mistake from a deletion: a ledger or a brief
    # citing D0xx that the register never had.
    while IFS=: read -r file cited; do
        if ! grep -qE "^\| $cited " docs/defects.md; then
            err "cites $cited, which docs/defects.md does not list" "$file"
            status=1
        fi
    done < <(grep -RonE '\bD[0-9]{3}\b' --include='*.md' \
        --exclude-dir=third_party --exclude=defects.md . |
        sed -E 's/^([^:]+):[0-9]+:/\1:/' | sort -u)
fi

if [[ $status -eq 0 ]]; then
    echo "docs-lint: ok"
fi
exit $status
