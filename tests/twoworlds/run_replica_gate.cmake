# Drives N1's acceptance gate (ADR 0069): one project, booted as an authority
# and as its replica in one process, and the replica must draw the authority's
# world. The host does the comparison; this script turns "no graphics device"
# into a skip, exactly as `run_two_worlds_gate.cmake` does.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DPROJECT=<examples/15-multiplayer> -DOUTPUT=<dir>
#         -P run_replica_gate.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required HOST PROJECT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_replica_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")

execute_process(
    # The harness's own size, not the host's window default: every pixel is
    # rendered twice a frame, on a software device on the Linux tier.
    COMMAND "${HOST}" "--replica-gate=${PROJECT}" "--two-worlds-out=${OUTPUT}" "--width=640" "--height=360"
    RESULT_VARIABLE host_result
    OUTPUT_VARIABLE host_output
    ERROR_VARIABLE host_output)

message("${host_output}")

if(host_result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
    message("LUAUG_TEST_SKIP: no graphics device on this machine")
    return()
endif()

if(NOT host_result EQUAL 0)
    message(FATAL_ERROR
        "replica seam: the gate exited ${host_result}.\n"
        "authority.png beside replica.png under ${OUTPUT} is where a replica that\n"
        "does not draw its authority's world shows it.")
endif()

foreach(image authority replica replica-first)
    if(NOT EXISTS "${OUTPUT}/${image}.png")
        message(FATAL_ERROR "replica seam: ${OUTPUT}/${image}.png was never written")
    endif()
endforeach()

message("replica seam: the replica draws the authority's world")
