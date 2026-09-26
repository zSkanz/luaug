# ADR 0091: the engine survives a lost graphics device -- a driver reset, which
# is what a surface shader that runs too long causes -- and does not draw the
# surface that was on screen again until it changes.
#
# The loss is simulated (`--simulate-device-loss`), the same path a real one
# takes from the RHI up: every call after it is dropped, the frame loop ends,
# and the process exits with its own code instead of dying inside the driver.
# A real reset was reproduced by hand, and the crash it caused is what this
# guards (the SDL patch 0002 and `IDevice::lost`).
#
#     cmake -DHOST=... -DSCENE=<a project with a surface> -DOUTPUT=<dir> -P ...

set(LUAUG_NO_DEVICE_EXIT_CODE 4)
set(LUAUG_DEVICE_LOST_EXIT_CODE 5)
foreach(required HOST SCENE OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_device_loss.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
set(cache "${OUTPUT}/surface-cache")

function(run_host label result_var output_var)
    execute_process(
        COMMAND "${HOST}" "${SCENE}" --headless --exit "--surface-cache=${cache}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    message("--- ${label} (exit ${result})\n${output}")
    if(result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
        message("LUAUG_TEST_SKIP: no graphics device on this machine")
        set(skipped TRUE PARENT_SCOPE)
    endif()
    if(output MATCHES "No shader compiler was found")
        message("LUAUG_TEST_SKIP: no shader compiler on this host")
        set(skipped TRUE PARENT_SCOPE)
    endif()
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

# 1. Compiled into this test's own cache, so the next run has the surface on
#    screen within a frame or two rather than after a cold compile.
run_host("warm the cache" result output --frames=20 "--screenshot=${OUTPUT}/warm.png")
if(skipped)
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "device loss: warming the cache exited ${result}")
endif()

# 2. Lost half way. Survived: its own exit code, not a crash; said, in words;
#    and the surface that was on screen written down.
run_host("lose the device" result output --frames=120 --simulate-device-loss=60)
if(NOT result EQUAL LUAUG_DEVICE_LOST_EXIT_CODE)
    message(FATAL_ERROR "device loss: expected exit ${LUAUG_DEVICE_LOST_EXIT_CODE} after a lost device, got ${result}")
endif()
if(NOT output MATCHES "The graphics device was lost")
    message(FATAL_ERROR "device loss: the loss was not reported")
endif()
if(NOT EXISTS "${cache}/quarantine.txt")
    message(FATAL_ERROR "device loss: nothing was held back")
endif()
file(READ "${cache}/quarantine.txt" held)
if(NOT held MATCHES "look\\.surface\\.hlsl")
    message(FATAL_ERROR "device loss: the surface on screen was not held back:\n${held}")
endif()

# 3. The next run -- the restarted editor -- draws the error surface in its
#    place and says why, and exits normally.
run_host("after the restart" result output --frames=20 "--screenshot=${OUTPUT}/after.png")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "device loss: the run after the loss exited ${result}")
endif()
if(NOT output MATCHES "held back")
    message(FATAL_ERROR "device loss: the held-back surface was compiled again")
endif()
message("device loss: survived, reported, and the surface on screen held back")
