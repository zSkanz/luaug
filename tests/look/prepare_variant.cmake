# Copies the look scene with its `Variant` line rewritten, for a golden.
#
#   cmake -DSCENE=<tests/look> -DVARIANT=<name> -DDESTINATION=<dir> -P prepare_variant.cmake
#
# Also `include()`d by `run_look_gate.cmake`. The same rewrite
# `tools/repo/look_captures.py` makes for the owner's captures, so a golden and
# the capture the owner approved are the same scene with the same line changed
# -- and CMake does it, because the gates run where Python may not be.

foreach(required SCENE VARIANT DESTINATION)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "prepare_variant.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${DESTINATION}")
file(MAKE_DIRECTORY "${DESTINATION}")
file(COPY "${SCENE}/content" "${SCENE}/src" DESTINATION "${DESTINATION}")

set(script "${DESTINATION}/src/scripts/init.luau")
file(READ "${script}" text)
string(REGEX MATCHALL "\nlocal Variant = \"[^\"]*\"\n" lines "${text}")
list(LENGTH lines found)
if(NOT found EQUAL 1)
    message(FATAL_ERROR "prepare_variant.cmake: the scene's `local Variant = ...` line is missing")
endif()
string(REGEX REPLACE "\nlocal Variant = \"[^\"]*\"\n" "\nlocal Variant = \"${VARIANT}\"\n" text "${text}")
file(WRITE "${script}" "${text}")
