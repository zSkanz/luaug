# L1 -- the SDL3 wrapper (ADR 0004).
#
# SDL3 is linked PRIVATE on purpose. The one public header that exposes SDL --
# sdl_interop.h, for the GPU backend and the app's ImGui glue -- therefore does
# not drag SDL's include directories along with it: a module that includes it
# must also link SDL3 itself. That makes crossing this seam a deliberate edit to
# a CMakeLists rather than something that can happen by adding an #include.

luaug_add_module(platform
    LAYER 1
    SOURCES
        src/platform.cpp
        src/window.cpp
        src/events.cpp
        src/console.cpp
    DEPS
        core
    PRIVATE_DEPS
        SDL3::SDL3-static
)

# The tests link SDL3 for the same reason the GPU backend will: they include
# sdl_interop.h, to push synthetic events through the real translation path
# rather than testing a mock of it.
luaug_add_module_tests(platform
    SOURCES
        tests/platform_tests.cpp
        tests/console_tests.cpp
    DEPS
        SDL3::SDL3-static
)

if(LUAUG_BUILD_TESTS)
    target_compile_definitions(luaug_platform_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json")
endif()
