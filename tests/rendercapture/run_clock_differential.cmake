# The differential render gate (roadmap M4.5): render one scene at two
# `ClockTime` values and require the two command streams to DIFFER.
#
# This is not a golden and deliberately not one. A golden compares a recording
# against a previous recording, so it certifies whatever was true when it was
# taken -- and for the whole of M4 what was true was that `Lighting` never
# reached the renderer. Six goldens, three camera angles, two lighting states,
# all green, all describing a sun pinned straight up.
#
# A differential compares two runs of the SAME build against each other. There is
# no recording to re-record, so there is nothing to accidentally bless.
#
# Two claims, and both matter:
#
#   1. The two frames differ. If `Lighting` stops reaching the frame, they do
#      not, and this goes red.
#   2. The two frames have the same number of commands. What changes with the
#      clock is the *content* of a frame, not its shape -- a scene that grew a
#      draw would satisfy claim 1 for the wrong reason, which is precisely how a
#      check ends up passing while proving nothing.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DSCRIPT=<project dir> -DOUTPUT=<jsonl>
#         -P run_clock_differential.cmake

foreach(required HOST SCRIPT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_clock_differential.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=3" --exit --rhi=capture "--capture-out=${OUTPUT}"
    RESULT_VARIABLE host_result
    OUTPUT_VARIABLE host_output
    ERROR_VARIABLE host_output)

message("${host_output}")

if(NOT host_result EQUAL 0)
    message(FATAL_ERROR "clock differential: the host exited ${host_result}")
endif()

file(STRINGS "${OUTPUT}" lines)

# Split on the frame markers the capture backend writes. Everything before the
# first one is device setup and belongs to no frame.
#
# The compared pair is the SECOND and THIRD frames, not the first two. The first
# frame of any run also creates every pipeline, shader, sampler and vertex buffer
# the scene needs, so it is structurally unlike a steady frame and would fail the
# equal-command-count claim below for a reason that has nothing to do with the
# sun.
set(frame_index -1)
set(frame_one "")
set(frame_two "")
foreach(line IN LISTS lines)
    if(line MATCHES "\"op\":\"beginFrame\"")
        math(EXPR frame_index "${frame_index} + 1")
    endif()
    if(frame_index EQUAL 1)
        list(APPEND frame_one "${line}")
    elseif(frame_index EQUAL 2)
        list(APPEND frame_two "${line}")
    endif()
endforeach()

if(NOT frame_index EQUAL 2)
    message(FATAL_ERROR "clock differential: expected exactly 3 frames, found ${frame_index}+1 in ${OUTPUT}")
endif()

list(LENGTH frame_one count_one)
list(LENGTH frame_two count_two)
if(NOT count_one EQUAL count_two)
    message(FATAL_ERROR
        "clock differential: the two frames issued different numbers of commands "
        "(${count_one} vs ${count_two}).\n"
        "Only the clock changed between them, so the scene should not have. "
        "Something other than the sun moved.")
endif()

# The frame counter itself is the one line that MUST differ, so it is dropped
# before the comparison -- otherwise this passes on the difference between
# `frame:0` and `frame:1` and never looks at anything else. That is the failure
# mode this whole file exists to catch, and it would be embarrassing here.
list(TRANSFORM frame_one REPLACE "\"frame\":[0-9]+" "\"frame\":X")
list(TRANSFORM frame_two REPLACE "\"frame\":[0-9]+" "\"frame\":X")

string(JOIN "\n" text_one ${frame_one})
string(JOIN "\n" text_two ${frame_two})

if(text_one STREQUAL text_two)
    message(FATAL_ERROR
        "clock differential: rendering at two different ClockTime values produced "
        "IDENTICAL command streams.\n"
        "  recorded: ${OUTPUT}\n"
        "The sun is not reaching the frame. This is the M4 defect: "
        "`Lighting` resolved to nothing and the renderer used its own defaults, "
        "which every golden then recorded faithfully.")
endif()

message("clock differential: two clock times, ${count_one} commands each, and they differ")
