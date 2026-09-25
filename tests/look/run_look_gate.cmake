# A golden of one look variant: prepare the scene, then hand it to a driver.
#
#   cmake -DSCENE=<tests/look> -DVARIANT=<name> -DDRIVER=<run_*_gate.cmake>
#         -DOUTPUT=<file> ...the driver's own -D arguments... -P run_look_gate.cmake
#
# `DRIVER` is `run_capture_gate.cmake` or `run_screenshot_gate.cmake`, unchanged:
# a look golden is an ordinary golden of a scene that happens to be prepared
# first, and the claim it makes is the driver's.

foreach(required SCENE VARIANT DRIVER OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_look_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

set(DESTINATION "${OUTPUT}.scene")
include("${CMAKE_CURRENT_LIST_DIR}/prepare_variant.cmake")
set(SCRIPT "${DESTINATION}")
include("${DRIVER}")
