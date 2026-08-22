# The graphics settings family, as a differential (roadmap M8, ADR 0044).
#
# A quality preset that is wired up and does nothing looks exactly like one that
# works: the frame still renders, the flags are still accepted, and the log still
# says which preset is in effect. The only instrument that can tell is one that
# looks at pixels, and the claim it has to make is the OPPOSITE of a golden's --
# two settings must NOT produce the same image.
#
# This is D043's lesson applied before the fact rather than after it. That defect
# shipped an instanced path drawing nothing, and three green instruments agreed
# with the empty frame because none of them compared an image against an image.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DIMGCMP=<imgcmp> -DSCRIPT=<project>
#         -DOUTPUT=<dir> -DFRAMES=<n> -P run_settings_differential.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required HOST IMGCMP SCRIPT OUTPUT FRAMES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_settings_differential.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(render quality out)
    execute_process(
        COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=${FRAMES}" --exit
                "--quality=${quality}" "--screenshot=${out}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    message("${output}")
    if(result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
        message("LUAUG_TEST_SKIP: no graphics device on this machine")
        return()
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "settings differential: the host exited ${result} at quality ${quality}")
    endif()
    if(NOT EXISTS "${out}")
        message(FATAL_ERROR "settings differential: no file written at quality ${quality}")
    endif()
endfunction()

render(low "${OUTPUT}/low.png")
if(NOT EXISTS "${OUTPUT}/low.png")
    # The skip was already printed by `render`; there is nothing to compare.
    return()
endif()
render(ultra "${OUTPUT}/ultra.png")

# Inverted on purpose: `imgcmp` reports success when two images MATCH, and a
# match is exactly the failure here.
execute_process(
    COMMAND "${IMGCMP}" "${OUTPUT}/low.png" "${OUTPUT}/ultra.png" --tolerance 2 --max-different-pixels 0
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output
    ERROR_VARIABLE compare_output)

message("${compare_output}")

if(compare_result EQUAL 0)
    message(FATAL_ERROR
        "settings differential: `low` and `ultra` rendered the same image.\n"
        "The quality family is accepted by the command line and reaches nothing that draws.")
endif()

message("settings differential: low and ultra differ, as they must")
