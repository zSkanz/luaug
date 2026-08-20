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
        src/file.cpp
        src/window.cpp
        src/events.cpp
        src/console.cpp
        src/crash.cpp
    DEPS
        core
    PRIVATE_DEPS
        SDL3::SDL3-static
)

# dbghelp is what writes a minidump, and it is Windows' own. Named here rather
# than in `app` because the call site is here: the layer that knows which OS
# this is, is the layer that names the OS's libraries.
if(WIN32)
    target_link_libraries(luaug_platform PRIVATE dbghelp)
endif()

# The tests link SDL3 for the same reason the GPU backend will: they include
# sdl_interop.h, to push synthetic events through the real translation path
# rather than testing a mock of it.
luaug_add_module_tests(platform
    SOURCES
        tests/platform_tests.cpp
        tests/console_tests.cpp
        tests/crash_tests.cpp
    DEPS
        SDL3::SDL3-static
)

if(LUAUG_BUILD_TESTS)
    target_compile_definitions(luaug_platform_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json")

    # A separate executable because it faults on purpose, and a doctest case
    # that faulted would take the runner with it. See `crash_probe.cpp`.
    add_executable(luaug_crash_probe tests/crash_probe.cpp)
    target_link_libraries(luaug_crash_probe PRIVATE luaug::warnings luaug::platform)

    add_test(NAME crash_gate
        COMMAND ${CMAKE_COMMAND}
            -DPROBE=$<TARGET_FILE:luaug_crash_probe>
            -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/crash-gate
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_crash_gate.cmake)
    set_tests_properties(crash_gate PROPERTIES TIMEOUT 60)
endif()
