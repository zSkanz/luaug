# Is there a picture of the chunk state at all?
#
# **E5 owed this screenshot and could not take it.** The gate row is "a
# screenshot of the chunk-state overlay is attached to the gate record", and the
# overlay it meant was an ImGui panel -- which cannot render headlessly, so the
# row sat PENDING from the day the milestone closed. Drawing the grid through
# `DebugDraw` puts it in the ordinary renderer, where `--headless --screenshot`
# already reaches: the version that is better for a person looking at a world is
# also the version a gate can capture, which is why the grid exists in the world
# rather than as a way to photograph a panel.
#
# A differential, and its claim is the opposite of a golden's: the same frame
# with the grid on and with it off must NOT be the same image. The same shape
# `run_local_shadow_gate` uses, and for the reason D043 records -- that defect
# shipped an instanced path drawing nothing, and three green instruments agreed
# with the empty frame because none of them compared an image against an image.
#
# The scene's own script opens `DebugService:ShowPanel("Streaming")` on its
# second tick, so the frame number selects which picture:
#
#   `--frames=1`  no grid: the world alone, and the reference
#   `--frames=3`  the grid over it
#
# Nothing else in the scene moves, so a difference can only be the grid.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DIMGCMP=<imgcmp> -DPROJECT=<project>
#         -DOUTPUT=<dir> -P run_chunk_grid_gate.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required HOST IMGCMP PROJECT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_chunk_grid_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

set(skipped OFF)

function(render frames out)
    execute_process(
        COMMAND "${HOST}" "${PROJECT}" --headless "--frames=${frames}" --exit "--screenshot=${out}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    message("${output}")
    if(result EQUAL LUAUG_NO_DEVICE_EXIT_CODE)
        message("LUAUG_TEST_SKIP: no graphics device on this machine")
        set(skipped ON PARENT_SCOPE)
        return()
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "chunk-grid gate: the host failed at ${frames} frame(s) (${result})")
    endif()
    if(NOT EXISTS "${out}")
        message(FATAL_ERROR "chunk-grid gate: no screenshot at ${out}")
    endif()
    # **The world has to be streaming for the picture to be of anything.** A
    # project whose scene did not partition would render a perfectly good frame
    # with no cells in it, the grid would draw nothing, and the differential
    # below would fail for a reason that has nothing to do with the grid.
    if(NOT output MATCHES "Streaming a world of")
        message(FATAL_ERROR
            "chunk-grid gate: the project did not stream, so there is no chunk state to draw.\\n"
            "  A scene that partitions into cells is what makes an index, and an index is what "
            "the grid is a picture of.")
    endif()
endfunction()

render(1 "${OUTPUT}/grid-off.png")
if(skipped)
    return()
endif()
render(3 "${OUTPUT}/grid-on.png")

# `imgcmp` exits non-zero when the images DIFFER, which is what a golden wants
# and the opposite of what this wants -- so a zero exit here is the failure.
execute_process(
    COMMAND "${IMGCMP}" "${OUTPUT}/grid-off.png" "${OUTPUT}/grid-on.png"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message("chunk grid: ${output}")
if(result EQUAL 0)
    message(FATAL_ERROR
        "chunk-grid gate: the frame is the same with the streaming grid on and off.\\n"
        "  ${OUTPUT}/grid-off.png\\n  ${OUTPUT}/grid-on.png\\n"
        "The grid is drawn through `DebugDraw` behind `DebugService:ShowPanel(\\"Streaming\\")`. "
        "Identical frames mean the switch is not reaching it, the host is not streaming, or the "
        "lines are being recorded into a buffer nobody submits -- and E5's screenshot row would "
        "go back to being a promise.")
endif()

message("chunk-grid gate: the grid changes the picture, and the picture is at ${OUTPUT}/grid-on.png")
