#!/usr/bin/env bash
# A skipped test is a test that did not run, and CTest counts one as a pass.
#
# Six gates in this repository answer `LUAUG_TEST_SKIP` when there is no
# graphics device, and `editor_shell` answers it when there is no display. On a
# machine that has both, every one of them is supposed to execute -- so a skip
# means the device stopped being found, or a test stopped being able to reach
# it, and the run is green either way. That is the failure this exists for.
#
#   assert-no-skips.sh <ctest-log> [expected-skip ...]
#
# Named skips are ALLOWED and anything else is a failure. Passing no names means
# no skip is acceptable, which is the right setting for a machine with a device.
# The names are matched exactly against CTest's own summary lines.
#
# **An expected skip that did NOT skip is not a failure.** A runner that grows a
# GPU should not need this file edited before it can be green; the list is a
# ceiling on what may skip, not a claim about what will.
set -euo pipefail

log="${1:?usage: assert-no-skips.sh <ctest-log> [expected-skip ...]}"
shift || true

if [[ ! -f "$log" ]]; then
    echo "assert-no-skips: no such log: $log" >&2
    exit 2
fi

# CTest prints `  3/51 Test  #3: name .......***Skipped   0.01 sec`. The name is
# the field between the colon and the run of dots.
mapfile -t skipped < <(
    grep -oE '^[[:space:]]*[0-9]+/[0-9]+[[:space:]]+Test[[:space:]]+#[0-9]+:[[:space:]]+[^[:space:]]+[[:space:]]+\.+\*+Skipped' "$log" \
        | sed -E 's/.*#[0-9]+:[[:space:]]+([^[:space:]]+)[[:space:]]+\.+\*+Skipped/\1/' \
        | sort -u
)

unexpected=()
for name in "${skipped[@]:-}"; do
    [[ -z "$name" ]] && continue
    allowed=0
    for expected in "$@"; do
        if [[ "$name" == "$expected" ]]; then
            allowed=1
            break
        fi
    done
    if [[ $allowed -eq 0 ]]; then
        unexpected+=("$name")
    fi
done

if [[ ${#unexpected[@]} -gt 0 ]]; then
    echo "assert-no-skips: ${#unexpected[@]} test(s) SKIPPED that were not expected to: ${unexpected[*]}" >&2
    echo "  CTest counts a skip as a pass, so this run reported green having not run them." >&2
    echo "  On a machine with a graphics device and a display these are supposed to execute." >&2
    echo "  If this environment genuinely cannot run one, name it in the caller's expected list" >&2
    echo "  -- which is a decision somebody writes down, rather than a silence." >&2
    exit 1
fi

if [[ ${#skipped[@]} -gt 0 && -n "${skipped[0]}" ]]; then
    echo "assert-no-skips: ok -- ${#skipped[@]} expected skip(s): ${skipped[*]}"
else
    echo "assert-no-skips: ok -- nothing skipped"
fi
