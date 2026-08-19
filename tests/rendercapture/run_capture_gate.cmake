# Drives the capture gate: record a frame's command stream, diff it against the
# golden.
#
# This is the *blocking* render-regression gate (docs/architecture.md §9). It
# needs no GPU and no driver, which is the whole point: a graphics vendor's
# update must not be able to turn the merge queue red. Real-image comparison is
# the agent's own verification tool and, from M4, a nightly non-blocking job.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DSCRIPT=<luau> -DGOLDEN=<jsonl> -DOUTPUT=<jsonl>
#         -DFRAMES=<n> -P run_capture_gate.cmake

foreach(required SCRIPT HOST GOLDEN OUTPUT FRAMES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_capture_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=${FRAMES}" --exit --rhi=capture "--capture-out=${OUTPUT}"
    RESULT_VARIABLE host_result
    OUTPUT_VARIABLE host_output
    ERROR_VARIABLE host_output)

message("${host_output}")

if(NOT host_result EQUAL 0)
    message(FATAL_ERROR "capture gate: the host exited ${host_result}")
endif()

# Byte-for-byte, no tolerance. The capture backend exists to make that possible:
# ids are sequential, floats are quantized and written from integers, enums are
# recorded by name. If two runs of one frame can differ here, the backend is
# broken and no amount of fuzzy comparison would make the gate meaningful.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}" "${GOLDEN}"
    RESULT_VARIABLE compare_result
    OUTPUT_QUIET
    ERROR_QUIET)

if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "capture gate: the recorded command stream does not match the golden.\n"
        "  recorded: ${OUTPUT}\n"
        "  golden:   ${GOLDEN}\n"
        "The files are JSON lines; diff them to see which call changed.\n"
        "If the change is intended, replace the golden and say why in the commit.")
endif()

message("capture gate: matches ${GOLDEN}")
