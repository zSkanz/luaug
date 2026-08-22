# Drives the editor-seam proof (roadmap M8; ADR 0017's standing condition).
#
# The host does the whole comparison itself -- see `engine/app/src/two_worlds.cpp`
# for why three sessions compared against each other cannot be split into
# separate CTest entries. This script exists for one reason: to turn "this
# machine has no usable GPU" into a skip rather than a red build, the same way
# the screenshot gate does.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DROOT=<tests/twoworlds> -DOUTPUT=<dir>
#         -P run_two_worlds_gate.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required HOST ROOT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_two_worlds_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")

execute_process(
    COMMAND "${HOST}" "--two-worlds=${ROOT}" "--two-worlds-out=${OUTPUT}"
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
        "editor seam: the proof exited ${host_result}.\n"
        "The four renders are under ${OUTPUT}; solo-a beside pair-a is where a\n"
        "leak between two worlds in one process shows up.")
endif()

# Written by the host before it compares, so their absence means it never got as
# far as rendering -- which every assertion above would have reported as a pass
# if the comparison had somehow been skipped.
foreach(image solo-a solo-b pair-a pair-b)
    if(NOT EXISTS "${OUTPUT}/${image}.png")
        message(FATAL_ERROR "editor seam: ${OUTPUT}/${image}.png was never written")
    endif()
endforeach()

message("editor seam: two worlds, two VMs, two targets, isolated")
