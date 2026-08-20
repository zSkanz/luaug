# L5 -- the game VM and every binding into it (architecture.md §2, §5).
#
# This is the ONLY module allowed to include a Luau header. Rule 2 of the
# layering says so, and the reason is not tidiness: `core` mirrors the `vector`
# memory layout without ever including `lua.h`, which is what lets tooling and
# tests use the engine's types with no VM at all. The layer checker enforces the
# direction; this CMakeLists is where the dependency is allowed to exist.
#
# It links `scene` and turns that module's POD change facts into signal fires.
# `scene` cannot do that itself and must not be able to: signal arguments are
# arbitrary script values, so a scene that owned the queue would hold references
# into the VM.

luaug_add_module(script
    LAYER 5
    SOURCES
        src/sandbox.cpp
        src/runtime.cpp
    DEPS
        core
        scene
)

target_link_libraries(luaug_script PRIVATE luaug::luau)

luaug_add_module_tests(script
    SOURCES
        tests/sandbox_tests.cpp
)

# The tests boot a VM directly rather than through `ScriptRuntime`, so that a
# sandbox failure has exactly one possible cause. That means they need Luau too.
if(LUAUG_BUILD_TESTS)
    target_link_libraries(luaug_script_tests PRIVATE luaug::luau)
endif()
