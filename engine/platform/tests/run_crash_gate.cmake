# Runs `luaug_crash_probe`, which faults on purpose, and asserts that the crash
# handler left an artifact behind.
#
# The handler's entire value is in what happens after a fault, so this is the
# only place it can be checked: a doctest case that faulted would take the runner
# down with it, and a case that merely called `installCrashHandler` would be
# asserting that a function returns true.
#
# Invoked as:
#   cmake -DPROBE=<luaug_crash_probe> -DWORKDIR=<dir> -P run_crash_gate.cmake

foreach(required PROBE WORKDIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_crash_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(
    COMMAND "${PROBE}" "${WORKDIR}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr)

string(STRIP "${probe_stdout}" artifact)
message("crash probe said: ${artifact}")
message("${probe_stderr}")

# 3 is the probe's own "the fault did not happen" exit. Distinguished from an
# ordinary failure because it means the artifact check below would be asserting
# nothing rather than failing.
if(probe_result EQUAL 3)
    message(FATAL_ERROR "crash gate: the probe did not fault, so nothing was tested")
endif()
if(probe_result EQUAL 2)
    message(FATAL_ERROR "crash gate: the probe could not install the handler")
endif()
if(probe_result EQUAL 0)
    message(FATAL_ERROR "crash gate: the probe exited cleanly, which it cannot do")
endif()

if(artifact STREQUAL "")
    message(FATAL_ERROR "crash gate: the probe printed no artifact path")
endif()

if(NOT EXISTS "${artifact}")
    message(FATAL_ERROR
        "crash gate: the process faulted and left nothing at\n"
        "  ${artifact}\n"
        "That is the whole failure this handler exists to prevent: a human hits a "
        "crash and has nothing to send.")
endif()

file(SIZE "${artifact}" artifact_size)
if(artifact_size EQUAL 0)
    message(FATAL_ERROR "crash gate: the artifact at ${artifact} is empty")
endif()

message("crash gate: faulted with ${probe_result}, wrote ${artifact_size} bytes")
