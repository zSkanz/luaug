# ADR 0091 end to end: a material in a project names a surface shader the USER
# wrote -- a copy of the built-in surface in the project's own content -- and
# the editor compiles it at run time. `usersurface/surfaced` must draw exactly
# what `usersurface/builtin` (the same material, naming no shader) draws.
#
# Identical is also what a surface that was never used would give, so the
# driver then proves it WAS used: a copy of the scene whose surface paints
# everything green must differ. Both halves or neither.
#
#     cmake -DHOST=... -DIMGCMP=... -DSCENES=<usersurface dir> -DOUTPUT=<dir> -P ...

set(LUAUG_NO_DEVICE_EXIT_CODE 4)
foreach(required HOST IMGCMP SCENES OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_user_surface_proof.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${SCENES}/surfaced/" DESTINATION "${OUTPUT}/painted")
file(READ "${OUTPUT}/painted/content/shaders/look.surface.hlsl" shader)
string(REPLACE "surface.BaseColor = Color * base.rgb;" "surface.BaseColor = float3(0.0, 1.0, 0.0);" shader "${shader}")
file(WRITE "${OUTPUT}/painted/content/shaders/look.surface.hlsl" "${shader}")

foreach(run surfaced builtin painted)
    if(run STREQUAL "painted")
        set(scene "${OUTPUT}/painted")
    else()
        set(scene "${SCENES}/${run}")
    endif()
    execute_process(
        COMMAND "${HOST}" "${scene}" --headless --frames=20 --exit "--screenshot=${OUTPUT}/${run}.png"
        RESULT_VARIABLE host_result
        OUTPUT_VARIABLE host_output
        ERROR_VARIABLE host_output)
    message("${host_output}")
    if(host_result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
        message("LUAUG_TEST_SKIP: no graphics device on this machine")
        return()
    endif()
    if(NOT host_result EQUAL 0)
        message(FATAL_ERROR "user surface proof: the ${run} render exited ${host_result}")
    endif()
    if(host_output MATCHES "No shader compiler was found")
        message("LUAUG_TEST_SKIP: no shader compiler on this host")
        return()
    endif()
endforeach()

execute_process(COMMAND "${IMGCMP}" "${OUTPUT}/surfaced.png" "${OUTPUT}/builtin.png" --tolerance 0
                        --max-different-pixels 0 --diff "${OUTPUT}/difference.png"
    RESULT_VARIABLE same_result OUTPUT_VARIABLE same_output ERROR_VARIABLE same_output)
message("${same_output}")
if(NOT same_result EQUAL 0)
    message(FATAL_ERROR "user surface proof: the user's copy of the built-in surface does not draw what the built-in "
                        "surface draws; the difference is at ${OUTPUT}/difference.png")
endif()

execute_process(COMMAND "${IMGCMP}" "${OUTPUT}/painted.png" "${OUTPUT}/builtin.png" --tolerance 0
                        --max-different-pixels 0
    RESULT_VARIABLE painted_result OUTPUT_VARIABLE painted_output ERROR_VARIABLE painted_output)
message("${painted_output}")
if(painted_result EQUAL 0)
    message(FATAL_ERROR "user surface proof: a surface painting everything green drew nothing different, so the "
                        "user's surface was never used and the identical pair proved nothing")
endif()
message("user surface proof: identical when copied, different when changed")
