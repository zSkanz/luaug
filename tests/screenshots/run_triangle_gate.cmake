# Drives the triangle sample's gate: render headless, assert on the frame, keep
# the PNG.
#
# A script rather than a bare add_test, for the same reason run_screenshot_gate
# is one: "the sample ran" and "the frame is right" are one claim, and the file
# it leaves behind has to be proven to come from this run rather than from the
# last one.
#
# Invoked as:
#   cmake -DSAMPLE=<luaug-triangle> -DOUTPUT=<png> -DFRAMES=<n>
#         -P run_triangle_gate.cmake

# The sample's exit code for "this machine has no usable GPU", matching the
# host's.
set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required SAMPLE OUTPUT FRAMES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_triangle_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

# Removed first, so a comparison can never pass on a file an earlier run wrote.
file(REMOVE "${OUTPUT}")

# `--verify` is where the pixel assertion lives: the sample reads its own frame
# back and checks that the centre is the triangle colour and every corner is
# still the clear colour. It is in the binary rather than here because CMake
# cannot read a pixel, and because the same check has to run on a phone.
execute_process(
    COMMAND "${SAMPLE}" --headless "--frames=${FRAMES}" --verify "--screenshot=${OUTPUT}"
    RESULT_VARIABLE sample_result
    OUTPUT_VARIABLE sample_output
    ERROR_VARIABLE sample_output)

message("${sample_output}")

# Exit 4 means the machine has no usable GPU. That is not a failure of anything
# under test, so it is reported as a skip rather than as red -- a build that goes
# red because a runner lacks a driver teaches people to ignore red builds.
#
# Signalled by printing a token CTest matches with SKIP_REGULAR_EXPRESSION rather
# than by an exit code: script mode has no way to choose its own exit code before
# CMake 3.29, and this file must not raise the project's floor of 3.28 for one
# branch.
if(sample_result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
    message("LUAUG_TEST_SKIP: no graphics device on this machine")
    return()
endif()

if(NOT sample_result EQUAL 0)
    message(FATAL_ERROR "triangle gate: the sample exited ${sample_result}")
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "triangle gate: the sample reported success but wrote no file")
endif()

# A frame that rendered nothing still produces a valid PNG, so size is not proof
# of content -- `--verify` above is. This only catches a truncated write.
file(SIZE "${OUTPUT}" output_size)
if(output_size EQUAL 0)
    message(FATAL_ERROR "triangle gate: ${OUTPUT} is empty")
endif()

message("triangle gate: the frame verifies, and is at ${OUTPUT}")
