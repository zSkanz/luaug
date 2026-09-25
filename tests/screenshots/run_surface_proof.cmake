# ADR 0091's proof that the surface shader contract is complete enough: the
# built-in surface, written as a surface shader using only
# `luaug/surface.hlsli` (`shaders/surface/pbr.surface.hlsl`), forced onto every
# static part of a scene, draws exactly what the built-in surface draws.
#
# A differential rather than a golden, so it holds on any GPU: the same scene is
# rendered twice on this machine, once with `--force-surface=pbr`, and the two
# must agree. A pixel that differs is the contract missing something a user
# would need -- it caught one on its first run: a surface could not tell a
# normal map that was not set from a flat one.
#
#     cmake -DHOST=... -DIMGCMP=... -DSCRIPT=<scene> -DFRAMES=<n> -DOUTPUT=<dir>
#           -P run_surface_proof.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)
foreach(required HOST IMGCMP SCRIPT FRAMES OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_surface_proof.cmake: -D${required}=... is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(builtin "${OUTPUT}/builtin.png")
set(surface "${OUTPUT}/surface.png")
file(REMOVE "${builtin}" "${surface}")

foreach(run builtin surface)
    set(extra "")
    if(run STREQUAL "surface")
        set(extra "--force-surface=pbr")
    endif()
    execute_process(
        COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=${FRAMES}" --exit "--screenshot=${${run}}" ${extra}
        RESULT_VARIABLE host_result
        OUTPUT_VARIABLE host_output
        ERROR_VARIABLE host_output)
    message("${host_output}")
    if(host_result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
        message("LUAUG_TEST_SKIP: no graphics device on this machine")
        return()
    endif()
    if(NOT host_result EQUAL 0)
        message(FATAL_ERROR "surface proof: the ${run} render exited ${host_result}")
    endif()
    # A surface that could not be built draws as the built-in one and would
    # pass by drawing nothing new. Said, so it fails instead.
    if(run STREQUAL "surface" AND host_output MATCHES "render.warn.surface_unavailable|could not be loaded or built")
        message(FATAL_ERROR "surface proof: the forced surface was not built, so nothing was proved")
    endif()
endforeach()

execute_process(
    COMMAND "${IMGCMP}" "${surface}" "${builtin}" --tolerance 0 --max-different-pixels 0
        "--diff" "${OUTPUT}/difference.png"
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output
    ERROR_VARIABLE compare_output)
message("${compare_output}")
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "surface proof: the built-in surface written as a surface shader does not draw what the built-in "
        "surface draws. The difference is at ${OUTPUT}/difference.png -- fix the contract, not the shader.")
endif()
message("surface proof: identical")
