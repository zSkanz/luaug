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
        src/animation.cpp
        src/binding.cpp
        src/datatypes.cpp
        src/instance_binding.cpp
        src/modules.cpp
        src/services.cpp
        src/signals.cpp
        src/tasks.cpp
        src/tweens.cpp
        src/sandbox.cpp
        src/reload_state.cpp
        src/runtime.cpp
    DEPS
        audio
        core
        input
        physics_api
        platform
        scene
)

target_link_libraries(luaug_script PRIVATE luaug::luau)

# `render` is linked into the TEST executable and nowhere else, the same
# arrangement `render`'s own tests use for `rhi_null` and for the same reason: a
# test binary is not a module. `script` binds `AnimationPlayer:LoadAnimation`,
# and the boot-time cross-check counts a binding whose class nothing declared as
# a mismatch -- so the fixture has to be able to register render's classes. The
# module itself still sees only `scene::AnimationHost`.
luaug_add_module_tests(script
    MODULES
        script
        render
    SOURCES
        tests/sandbox_tests.cpp
        tests/datatype_tests.cpp
        tests/instance_binding_tests.cpp
        tests/service_tests.cpp
        tests/signal_tests.cpp
        tests/task_tests.cpp
)

# The sandbox tests boot a VM directly rather than through `ScriptRuntime`, so
# that a sandbox failure has exactly one possible cause. That means they need
# Luau too.
#
# The catalog is a real dependency of the binding tests rather than a
# convenience: an error is identified by its `[key]` prefix, and the prefix is
# the catalog's own name for the key (ADR 0019). Without it every raise reports
# `[i18n:missing:xxxxxxxx]` and a test asserting on a key would pass only by
# matching nothing.
if(LUAUG_BUILD_TESTS)
    target_link_libraries(luaug_script_tests PRIVATE luaug::luau)
    target_compile_definitions(luaug_script_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json")
endif()
