# Runs `luaug_crash_probe`, which dies on purpose two different ways, and asserts
# that the crash handler left behind something a person can act on.
#
# The handler's entire value is in what happens after the process is already
# lost, so this is the only place it can be checked: a doctest case that faulted
# would take the runner down with it, and a case that merely called
# `installCrashHandler` would be asserting that a function returns true.
#
# **Two modes, because they are two different failures**, and one of them had no
# instrument at all until it produced a real defect report nobody could read: a
# person hit a crash, sent the `.dmp`, and there was no debugger on the machine
# to open it with. Where each mode ends up differs by platform and the answer
# came from running this rather than from reading the runtime -- see the note
# above the throw case at the bottom.
#
# **And the note's CONTENT is asserted, not just its existence.** A note that
# exists and says nothing is the same failure wearing a passing gate: the whole
# point is that somebody with no debugger installed can read what broke.
#
# Invoked as:
#   cmake -DPROBE=<luaug_crash_probe> -DWORKDIR=<dir> -P run_crash_gate.cmake

foreach(required PROBE WORKDIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_crash_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

# Runs one mode and leaves `artifact` and `note` set to what the probe itself
# reported. Asserting against the handler's own answer rather than against a
# filename this script reconstructs is deliberate: a handler that wrote
# somewhere else entirely would still pass a reconstructed check.
function(run_probe mode out_artifact out_note)
    set(work "${WORKDIR}/${mode}")
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")

    execute_process(
        COMMAND "${PROBE}" "${work}" "${mode}"
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_stdout
        ERROR_VARIABLE probe_stderr)

    string(STRIP "${probe_stdout}" trimmed)
    string(REPLACE "\n" ";" lines "${trimmed}")
    list(LENGTH lines line_count)
    if(line_count LESS 2)
        message(FATAL_ERROR "crash gate (${mode}): the probe printed no artifact and note paths")
    endif()
    list(GET lines 0 artifact)
    list(GET lines 1 note)
    string(STRIP "${artifact}" artifact)
    string(STRIP "${note}" note)

    message("crash probe (${mode}) exited ${probe_result}")
    message("  artifact: ${artifact}")
    message("  note:     ${note}")
    if(NOT probe_stderr STREQUAL "")
        message("  stderr:   ${probe_stderr}")
    endif()

    # 3 is the probe's own "it did not die" exit, distinguished from an ordinary
    # failure because it means every check below would be asserting nothing.
    if(probe_result EQUAL 3)
        message(FATAL_ERROR "crash gate (${mode}): the probe did not die, so nothing was tested")
    endif()
    if(probe_result EQUAL 2)
        message(FATAL_ERROR "crash gate (${mode}): the probe could not install the handler")
    endif()
    if(probe_result EQUAL 0)
        message(FATAL_ERROR "crash gate (${mode}): the probe exited cleanly, which it cannot do")
    endif()

    set(${out_artifact} "${artifact}" PARENT_SCOPE)
    set(${out_note} "${note}" PARENT_SCOPE)
endfunction()

# Reads the note and requires every phrase in `expected` to appear in it, with a
# failure message that says what a missing one MEANS rather than which string was
# absent -- because the reader of this failure is the next person to change the
# handler, and they need to know what they took away.
function(require_note note)
    if(NOT EXISTS "${note}")
        message(FATAL_ERROR
            "crash gate: the process died and left no note at\n"
            "  ${note}\n"
            "That is the whole failure this handler exists to prevent: somebody hits a "
            "crash and has nothing readable to send.")
    endif()
    file(SIZE "${note}" note_size)
    if(note_size EQUAL 0)
        message(FATAL_ERROR "crash gate: the note at ${note} is empty")
    endif()

    file(READ "${note}" text)
    foreach(phrase ${ARGN})
        string(FIND "${text}" "${phrase}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR
                "crash gate: the note does not mention '${phrase}'.\n"
                "The note is what a person reads instead of opening a debugger, and on the "
                "machine this engine is developed on there is no debugger installed. A note "
                "without this in it sends them back to guessing.\n"
                "--- what it said instead ---\n${text}")
        endif()
    endforeach()
endfunction()

# --- A fault ------------------------------------------------------------------
run_probe(fault fault_artifact fault_note)

if(NOT EXISTS "${fault_artifact}")
    message(FATAL_ERROR
        "crash gate: the process faulted and left nothing at\n  ${fault_artifact}")
endif()
file(SIZE "${fault_artifact}" fault_size)
if(fault_size EQUAL 0)
    message(FATAL_ERROR "crash gate: the artifact at ${fault_artifact} is empty")
endif()

if(WIN32)
    # The exception's plain name, the address it touched, and a stack. Each one
    # is a question a person asks in order and could not answer before.
    require_note("${fault_note}" "access violation" "Tried to write" "Stack")
else()
    require_note("${fault_note}" "signal")
endif()
message("crash gate: faulted, wrote ${fault_size} bytes of dump and a readable note")

# --- A throw nobody caught ----------------------------------------------------
run_probe(throw throw_artifact throw_note)

# **What is asserted here is the STACK, and that is a finding rather than a
# preference.** On MSVC the C++ runtime wraps `main` in its own `__try`, so a
# throw nobody catches reaches the unhandled-exception filter and the process
# dies there without `std::terminate` ever running -- which means the `what()`
# string is not available to write. What IS available, and what this requires,
# is the exception named in plain words and a stack whose frames include the
# source file and line the throw came from. That is the question a person asks
# first, and before this note existed they could not answer it at all.
if(WIN32)
    require_note("${throw_note}" "an uncaught C++ exception" "crash_probe.cpp" "Stack")
else()
    # POSIX reaches `terminate`, so there the message itself must survive.
    require_note("${throw_note}" "crash probe threw this on purpose")
endif()
message("crash gate: an uncaught throw left a note naming itself and where it came from")
