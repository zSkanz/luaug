# samples/triangle -- the M4 Android checkpoint's artifact (roadmap M4).
#
# It links core + platform + rhi_api + rhi_sdlgpu + render + asset and stops
# there. No `luaug::app`, no `luaug::script`, no Luau VM: the sample exists to
# ask a device one question -- does SDL3 GPU rasterize here (ADR 0005) -- and a
# binary that also booted a VM would have a dozen ways to fail that have nothing
# to do with the answer.
#
# `luaug::render` brings `luaug::scene` with it, because that is what the module
# depends on; the sample uses neither, and the linker drops what nothing calls.

if(NOT LUAUG_RHI_SDLGPU)
    # Said out loud rather than silently skipped: this sample IS the SDL_GPU
    # backend, so a build without it has no triangle to draw and the absence
    # should not be discovered as a missing binary.
    message(STATUS "LuauG: LUAUG_RHI_SDLGPU is off -- the triangle sample is not built")
    return()
endif()

add_executable(luaug_triangle_sample src/main.cpp)
set_target_properties(luaug_triangle_sample PROPERTIES OUTPUT_NAME luaug-triangle)

target_link_libraries(luaug_triangle_sample PRIVATE
    luaug::warnings
    luaug::build_info
    luaug::core
    luaug::platform
    luaug::rhi_api
    luaug::rhi_sdlgpu
    luaug::render
    luaug::asset
)

# The sample's own content, staged beside its own executable, because
# `platform::paths()` derives the content directory from the running binary's
# location -- the shape a packaged build has, and the shape an APK will need.
#
# Only `triangle.hlsl`: the shader pack a sample carries should be the shaders
# it draws with, so a missing blob on a device is a fact about this pipeline
# rather than about the engine's whole shader set.
# The sample's shader lives with the sample, not in `shaders/src/`. That
# directory is the ENGINE's shader source and the host globs all of it, so a
# sample-only shader parked there was compiled into `luaug-host`'s content
# directory and would have ridden into a shipped pack at M8 -- content nothing
# asks for, inherited rather than decided.
luaug_add_shaders(luaug_triangle_sample GLOB "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.hlsl")

# The message catalog, for the same reason the host stages it: engine errors are
# TextKeys, and without the catalog a device that failed to create a GPU device
# would report a hash instead of saying so.
add_custom_command(TARGET luaug_triangle_sample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/i18n/en.json"
        "$<TARGET_FILE_DIR:luaug_triangle_sample>/content/i18n/en.json"
    COMMENT "Staging the engine message catalog for the triangle sample")

if(LUAUG_BUILD_TESTS)
    # Renders headless, writes a PNG, and asserts on the frame it rendered.
    #
    # The assertion is the sample's own `--verify` -- centre is the triangle
    # colour, all four corners are still the clear colour -- rather than an
    # imgcmp comparison against a checked-in golden. That is a deliberate
    # departure from `screenshot_gate`, and the reason is written in that gate's
    # own comment: a pixel golden is tied to the GPU that recorded it, and
    # geometry does not survive the move, which is why `screenshot_gate` had to
    # be excluded from CI the moment its frame contained edges. This sample's
    # frame is nothing but edges, and it must also give a verdict on an Android
    # device where no golden and no CTest exist.
    #
    # Two probes rather than one, so both of the ways a renderer breaks are
    # caught: a pipeline that drew nothing fails the centre, and one that drew
    # over the whole target fails the corners.
    add_test(NAME triangle_sample
        COMMAND ${CMAKE_COMMAND}
            -DSAMPLE=$<TARGET_FILE:luaug_triangle_sample>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/triangle-sample.png
            -DFRAMES=3
            -P ${CMAKE_SOURCE_DIR}/tests/screenshots/run_triangle_gate.cmake)

    # Skips rather than fails where there is no GPU; see the driver script.
    set_tests_properties(triangle_sample PROPERTIES
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 120)
endif()
