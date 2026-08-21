# L4 -- architecture.md §2 eventually gives render core, jobs, platform,
# rhi_api, asset and scene. It now has scene as well, for `render::extract`: the
# POD snapshot rendering reads instead of walking the ECS (ADR 0027), and
# `platform`, for `ShaderLibrary`: a shader blob inside an Android APK is a zip
# entry, not a file, and platform::readFile is the one seam that knows the
# difference (engine/platform/include/luaug/platform/file.h). The rest arrive
# with the code that uses them, because a dependency with no consumer is
# speculative.
#
# It links `rhi_api` and no backend. That is the seam working: this module has
# no idea which device it is drawing with, and `app` is the only place that does
# (ADR 0023).

luaug_add_module(render
    LAYER 4
    SOURCES
        src/debug_draw.cpp
        src/environment.cpp
        src/shader_library.cpp
        src/debug_renderer.cpp
        src/render_world.cpp
        src/mesh_cache.cpp
        src/lighting.cpp
        src/native_accessors.cpp
        src/renderer_default.cpp
        src/mesh_loader.cpp
        src/scene_types.cpp
        src/shadow.cpp
        src/ui_renderer.cpp
        src/animation.cpp
        generated/class_descriptors.gen.cpp
    DEPS
        asset
        core
        jobs
        platform
        rhi_api
        scene
)

# The generated reflection tables for the classes this module owns
# (architecture.md §4). Checked in and freshness-gated by
# scripts/gates/luau-check.sh, like scene's.
#
# No include directory is added for them, deliberately: every module's generated
# header is named `class_descriptors.gen.h`, so a second one on any include path
# would turn `#include "class_descriptors.gen.h"` into a search-order lottery in
# `app`, which links both. The two files that need this one reach it by a
# relative path instead, and nothing outside `render` may include it at all --
# `luaug/render/scene_types.h` is the whole public surface.

# `rhi_null` is linked into the TEST executable and nowhere else. ADR 0023's
# rule is that a backend is linked only by `app`; a test binary is not a module,
# and `MeshCache` cannot be exercised at all without a device that hands back
# distinct buffer handles. `luaug_render` itself still sees only the seam.
luaug_add_module_tests(render
    MODULES
        render
        rhi_null
    SOURCES
        tests/debug_draw_tests.cpp
        tests/environment_tests.cpp
        tests/shader_library_tests.cpp
        tests/render_world_tests.cpp
        tests/mesh_cache_tests.cpp
        tests/lighting_tests.cpp
        tests/shadow_tests.cpp
        tests/animation_tests.cpp
)
