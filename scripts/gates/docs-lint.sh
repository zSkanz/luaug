#!/usr/bin/env bash
# The documentation gate: relative links resolve, pinned versions are named
# consistently, no stray references to the platform R7 keeps us clear of, the
# ledger keeps its shape.
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
# legitimately contain both broken relative links and the name R7 sweeps for
# (Luau's upstream is that platform's). Linting them would be meaningless and a
# permanent source of
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
# **The name is assembled rather than written**, so this file -- which is code
# -- does not spell the thing it forbids. The owner's rule is that no corner of
# the code refers to that platform, and the gate enforcing it is not an
# exception to it.
#
# What may name it is DOCUMENTATION a person reads to decide whether this engine
# is for them: the migration guide, the design notes that explain a divergence,
# the ledger, the art briefs that say what a mark must not resemble. Nothing
# executable, nothing generated from code, and no configuration file is on that
# list any more -- the generators emit the migration section under a neutral
# slug, and the editor settings explain themselves without naming anybody.
echo "== legal sweep (R7) =="
vendor="$(printf '%s%s' 'rob' 'lox')"
allowed='^(\./)?(README\.md|CONTRIBUTING\.md|NOTICE|MASTER_PROMPT\.md|CLAUDE\.md|PROGRESS\.md|docs/|templates/README\.md|examples/README\.md|tests/README\.md|runtime/README\.md|api/README\.md|engine/README\.md|tools/README\.md|third_party/README\.md|branding/README\.md|art/)'
while IFS= read -r f; do
    if ! [[ "$f" =~ $allowed ]]; then
        err "the platform R7 forbids is named outside the allowed docs set" "$f"
        status=1
    fi
    # Tracked files only. R7 is about what this repository publishes, and a
    # working tree also holds things it does not: a developer's local editor
    # state, a scratch file, an ignored tool config. Sweeping those made the
    # gate depend on whose machine it ran on -- `.claude/settings.local.json`,
    # which is globally gitignored, turned it red on this one.
done < <(git ls-files -z -- . ':(exclude)third_party' ':(exclude).github' \
    | xargs -0 grep -liE "$vendor" 2>/dev/null || true)

# **The indirect names, in code.** A type or class name that exists only on that
# platform, its editor's product name standing alone, or its unit of length, is
# the same reference
# spelled another way -- and one of each had survived the sweep above because it
# never contained the word. Code only: the documentation set may still explain
# a divergence by naming what it diverges from. "Visual Studio" and "Android
# Studio" are toolchains, not the reference, and are excluded by name.
echo "== legal sweep (R7, indirect names in code) =="
indirect="$(printf '%s|%s|%s|%s' 'RBX[A-Z]' 'Bindable(Event|Function)' '(^|[^A-Za-z])Studio([^A-Za-z]|$)'     '(^|[^A-Za-z])[Ss]tuds?([^A-Za-z]|$)')"
while IFS= read -r hit; do
    case "$hit" in
    *"Visual Studio"* | *"Android Studio"*) continue ;;
    esac
    err "an indirect reference R7 forbids: ${hit#*:*:}" "${hit%%:*}"
    status=1
done < <(git ls-files -z -- engine runtime shaders tools api tests examples templates i18n cmake scripts \
    ':(exclude)*.md' ':(exclude)*.png' ':(exclude)scripts/gates/docs-lint.sh' \
    | xargs -0 grep -nE "$indirect" 2>/dev/null || true)

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
    #
    # **`defects.md` is swept too, and excluding it was a hole rather than a
    # saving.** A row citing another row is the commonest citation in this
    # repository -- one defect is regularly the other half of an earlier one, or
    # the reason an earlier one was looked for -- and none of those was checked
    # by anything. D121 cited a defect that had never been given a row at all
    # and went through a green gate for a day.
    #
    # What the exclusion was protecting against is real, and is handled by
    # stripping instead: a ROW DEFINITION is not a citation. `| D121 |` at the
    # start of a line is the register making the claim, not a document relying
    # on one, and a row allowed to satisfy itself proves nothing. So the id
    # field comes off the front of each row and everything after it is prose,
    # read exactly as prose is read anywhere else.
    while IFS=: read -r file cited; do
        if ! grep -qE "^\| $cited " docs/defects.md; then
            err "cites $cited, which docs/defects.md does not list" "$file"
            status=1
        fi
    done < <({
        grep -RonE '\bD[0-9]{3}\b' --include='*.md' \
            --exclude-dir=third_party --exclude=defects.md . |
            sed -E 's/^([^:]+):[0-9]+:/\1:/'
        sed -E 's/^\| D[0-9]{3} \|//' docs/defects.md |
            { grep -oE '\bD[0-9]{3}\b' || true; } |
            sed -E 's|^|./docs/defects.md:|'
    } | sort -u)

    # And an unfilled placeholder is a citation too -- of nothing.
    #
    # The check above needs three digits to see a citation, so `D-`, which is
    # what somebody writes when the number is not known yet, was invisible to it
    # twice over: in the file it did not read, in a shape it could not match.
    # Both of the ones that survived were pointing at real defects; one of them
    # had no row anywhere, which is the disappearance the register exists to
    # stop.
    #
    # Two things are deliberately NOT a placeholder. A letter after the hyphen
    # is an ordinary hyphenated word -- `D-numbered` work, in a brief -- and a
    # backtick after it means the text is QUOTING the placeholder rather than
    # leaving one, which the mission file and D136 both do on purpose. That is
    # the same carve-out the legal sweep above makes: a document that has to
    # name what is forbidden must be able to spell it.
    while IFS= read -r file; do
        err "cites 'D-', which is a placeholder where a defect id belongs" "$file"
        status=1
    done < <(grep -RlE '\bD-([^0-9A-Za-z`]|$)' --include='*.md' \
        --exclude-dir=third_party . | sort -u)
fi

if [[ $status -eq 0 ]]; then
    echo "docs-lint: ok"
fi
exit $status
