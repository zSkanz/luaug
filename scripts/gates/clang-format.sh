#!/usr/bin/env bash
# The C++ formatting gate. `architecture.md` §9 has listed a clang-format check
# since M0 and there was none until M5 -- deliberately: turning it on while the
# renderer was being written would have bought a milestone of diff noise for a
# style rule, so M4's brief moved it to the first act of the next milestone, on
# a quiet tree.
#
# This file is the gate. `.github/workflows/ci.yml` runs it and so does
# `scripts/localgate.ps1`, which is the point -- one implementation, no copy to
# drift.
#
#   scripts/gates/clang-format.sh          # check; prints every file that differs
#   scripts/gates/clang-format.sh --fix    # rewrite them in place
#
# THE VERSION IS PART OF THE STYLE. clang-format's output changes between major
# releases, so an unpinned binary makes "formatted" mean something different on
# every machine and the gate becomes a coin toss. The pin below is Ubuntu
# 24.04's, which is what the Tier-2 image and `ubuntu-latest` both carry; the
# dev machine's Visual Studio ships 20 and would reformat the tree back and
# forth forever, so this refuses to run on the wrong major rather than
# formatting with it.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

RequiredMajor=18

fix=0
case "${1:-}" in
--fix) fix=1 ;;
"") ;;
*)
    echo "usage: $0 [--fix]" >&2
    exit 2
    ;;
esac

err() { # message, [file]
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error ${2:+file=$2}::$1"
    fi
    echo "  FAIL: ${2:+$2: }$1" >&2
}

# --- The binary, at the pinned major ----------------------------------------
binary="${CLANG_FORMAT:-}"
if [[ -z "$binary" ]]; then
    for candidate in "clang-format-$RequiredMajor" clang-format; do
        if command -v "$candidate" >/dev/null 2>&1; then
            binary="$candidate"
            break
        fi
    done
fi

if [[ -z "$binary" ]]; then
    err "clang-format-$RequiredMajor is not installed. On Ubuntu: apt-get install clang-format-$RequiredMajor. On Windows, run the gate through the Tier-2 container (scripts/localgate.ps1 -Only format)."
    exit 1
fi

version="$("$binary" --version)"
major="$(sed -n 's/.*clang-format version \([0-9]\{1,\}\).*/\1/p' <<<"$version")"
if [[ "$major" != "$RequiredMajor" ]]; then
    err "$binary is '$version', and this repository formats with clang-format $RequiredMajor. A different major produces a different tree, so the two would fight over every file. Set CLANG_FORMAT to a $RequiredMajor binary, or run scripts/localgate.ps1 -Only format."
    exit 1
fi

# --- What it covers ---------------------------------------------------------
# Everything we wrote, and nothing we did not:
#
#   third_party/  is vendored at a pinned commit and is never edited in place
#                 (R13, ADR 0021). Formatting it would be exactly that edit.
#   */generated/  is written by api/generator and diff-checked against a fresh
#                 run by luau-check.sh. Reformatting a generated file makes that
#                 check fail on the next regeneration, so the generator's output
#                 is the authority on its own layout.
#
# `--others --exclude-standard` alongside `--cached` so a file that is written
# but not yet staged is checked too. Without it this gate is blind to exactly
# the files most likely to be unformatted -- the new ones -- and it passed a
# whole new module on its first run for that reason.
mapfile -t files < <(
    git ls-files --cached --others --exclude-standard -- '*.cpp' '*.h' '*.hpp' '*.cc' '*.mm' |
        grep -v '^third_party/' |
        grep -v '/generated/' |
        LC_ALL=C sort -u
)

if [[ ${#files[@]} -eq 0 ]]; then
    err "no C++ sources matched -- the gate would pass by doing nothing, which is the one way a gate is worse than absent."
    exit 1
fi

if [[ $fix -eq 1 ]]; then
    echo "== clang-format --fix ($binary, ${#files[@]} files) =="
    "$binary" -i "${files[@]}"
    echo "  rewritten in place; review with git diff"
    exit 0
fi

echo "== clang-format ($binary, ${#files[@]} files) =="

# --dry-run -Werror reports every file that differs rather than stopping at the
# first, and the exit code is the gate. What gets replayed is the FILE list, not
# clang-format's own output: it emits one diagnostic per offending line, and a
# gate that answers "your tree is unformatted" with four thousand lines is one
# nobody reads to the end.
status=0
report="$("$binary" --dry-run -Werror "${files[@]}" 2>&1)" || status=1

if [[ $status -ne 0 ]]; then
    offenders="$(sed -n 's/^\(.*\):[0-9][0-9]*:[0-9][0-9]*: \(warning\|error\): code should be clang-formatted.*$/\1/p' <<<"$report" | LC_ALL=C sort -u)"
    if [[ -z "$offenders" ]]; then
        # Some other failure -- a parse error, an unreadable file. Then the raw
        # report IS the message, and swallowing it would leave nothing to act on.
        printf '%s\n' "$report" >&2
        err "clang-format failed for a reason other than formatting"
        exit 1
    fi
    while IFS= read -r file; do
        err "not clang-formatted" "$file"
    done <<<"$offenders"
    echo "" >&2
    echo "  $(wc -l <<<"$offenders") of ${#files[@]} file(s) differ." >&2
    echo "  Fix: scripts/gates/clang-format.sh --fix   (on Windows: scripts/localgate.ps1 -Only format, which runs it in the container at the pinned major)" >&2
    exit 1
fi

echo "  ok: ${#files[@]} files"
