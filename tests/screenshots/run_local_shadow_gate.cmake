# Do `SpotLight.Shadows` and `PointLight.Shadows` change the picture?
#
# **The only question worth asking about them**, and the one nothing could ask
# for three milestones. Both properties were stored, plumbed all the way to
# `RenderLight::shadows` and read by no pass, and every instrument short of
# comparing two images would have passed throughout: the frame rendered, the
# property read back what was written, the light still lit.
#
# So this is a differential and its claim is the opposite of a golden's -- two
# runs must NOT produce the same image. The same shape `run_settings_differential`
# uses, and for the same reason D043 records: that defect shipped an instanced
# path drawing nothing, and three green instruments agreed with the empty frame
# because none of them compared an image against an image.
#
# The scene renders one case per frame number: 1 is the spot casting, 2 is the
# point casting, 3 is neither. Its sun is switched off entirely, so a difference
# between any two of them cannot be the cascades.
#
# Invoked as:
#   cmake -DHOST=<luaug-host> -DIMGCMP=<imgcmp> -DSCRIPT=<project>
#         -DOUTPUT=<dir> -P run_local_shadow_gate.cmake

set(LUAUG_NO_DEVICE_EXIT_CODE 4)

foreach(required HOST IMGCMP SCRIPT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_local_shadow_gate.cmake: -D${required}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

set(skipped OFF)

function(render frames out)
    execute_process(
        COMMAND "${HOST}" "${SCRIPT}" --headless "--frames=${frames}" --exit "--screenshot=${out}"
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
        message(FATAL_ERROR "local-shadow gate: the host failed at ${frames} frame(s) (${result})")
    endif()
    if(NOT EXISTS "${out}")
        message(FATAL_ERROR "local-shadow gate: no screenshot at ${out}")
    endif()
endfunction()

render(1 "${OUTPUT}/spot.png")
if(skipped)
    return()
endif()
render(2 "${OUTPUT}/point.png")
render(3 "${OUTPUT}/none.png")

# `imgcmp` exits non-zero when the images DIFFER, which is what a golden wants
# and the opposite of what this wants -- so a zero exit here is the failure.
function(require_different a b what)
    execute_process(
        COMMAND "${IMGCMP}" "${a}" "${b}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    message("${what}: ${output}")
    if(result EQUAL 0)
        message(FATAL_ERROR
            "local-shadow gate: ${what} produced the SAME image with the property on and off.\n"
            "  ${a}\n  ${b}\n"
            "That is what this gate exists to refuse: the property is accepted, read back and "
            "plumbed to the renderer, and nothing acts on it. It looked exactly like this for "
            "three milestones.")
    endif()
endfunction()

require_different("${OUTPUT}/spot.png" "${OUTPUT}/none.png" "SpotLight.Shadows")
require_different("${OUTPUT}/point.png" "${OUTPUT}/none.png" "PointLight.Shadows")

message("local-shadow gate: both light kinds occlude")
