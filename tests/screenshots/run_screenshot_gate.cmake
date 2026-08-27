# Drives the screenshot gate: render headless, then compare against the golden.
#
# A script rather than two CTest entries, because the two halves are one claim
# -- "the engine still draws what it drew" -- and splitting them would let the
# comparison pass on a stale file from a previous run.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DSCRIPT=<luau> -DIMGCMP=<imgcmp> -DGOLDEN=<png>
#         -DOUTPUT=<png> -DFRAMES=<n> [-DTOLERANCE=<n>] -P run_screenshot_gate.cmake
#
# `TOLERANCE` is optional and defaults to 2, which is the right answer when a
# golden has to survive a move between GPUs. The lavapipe suite passes 0: it
# compares a software rasterizer against itself, which is bit-identical, and
# there is nothing there for a tolerance to absorb except a real change.

# The host's exit code for "this machine has no usable GPU".
set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required SCRIPT HOST IMGCMP GOLDEN OUTPUT FRAMES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_screenshot_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=${FRAMES}" --exit "--screenshot=${OUTPUT}"
    RESULT_VARIABLE host_result
    OUTPUT_VARIABLE host_output
    ERROR_VARIABLE host_output)

message("${host_output}")

# Exit 4 means the machine has no usable GPU. That is not a failure of anything
# under test, so it is reported as a skip rather than as red -- a build that
# goes red because a runner lacks a driver teaches people to ignore red builds.
#
# Signalled by printing a token CTest matches with SKIP_REGULAR_EXPRESSION
# rather than by an exit code: script mode has no way to choose its own exit
# code before CMake 3.29, and this file must not raise the project's floor of
# 3.28 for one branch.
if(host_result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
    message("LUAUG_TEST_SKIP: no graphics device on this machine")
    return()
endif()

if(NOT host_result EQUAL 0)
    message(FATAL_ERROR "screenshot gate: the host exited ${host_result}")
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "screenshot gate: the host reported success but wrote no file")
endif()

# Tolerance 2 per channel by default: GPUs round the last bit of a unorm
# conversion differently, and a gate that fires on that is a gate that gets
# switched off. Zero differing pixels, though -- a real change is never one pixel.
if(NOT DEFINED TOLERANCE)
    set(TOLERANCE 2)
endif()

execute_process(
    COMMAND "${IMGCMP}" "${OUTPUT}" "${GOLDEN}" --tolerance "${TOLERANCE}" --max-different-pixels 0
        "--diff" "${OUTPUT}.diff.png"
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output
    ERROR_VARIABLE compare_output)

message("${compare_output}")

if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "screenshot gate: ${OUTPUT} does not match ${GOLDEN}\n"
        "A diff image is at ${OUTPUT}.diff.png.\n"
        "If the change is intended, replace the golden and say so in the commit.")
endif()

message("screenshot gate: matches ${GOLDEN}")
