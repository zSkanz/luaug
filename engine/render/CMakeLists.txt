# L4 -- architecture.md §2 eventually gives render core, jobs, platform,
# rhi_api, asset and scene. It now has scene as well, for `render::extract`: the
# POD snapshot rendering reads instead of walking the ECS (ADR 0027). The rest
# arrive with the code that uses them, because a dependency with no consumer is
# speculative.
#
# It links `rhi_api` and no backend. That is the seam working: this module has
# no idea which device it is drawing with, and `app` is the only place that does
# (ADR 0023).

luaug_add_module(render
    LAYER 4
    SOURCES
        src/debug_draw.cpp
        src/shader_library.cpp
        src/debug_renderer.cpp
        src/render_world.cpp
        src/mesh_cache.cpp
    DEPS
        asset
        core
        rhi_api
        scene
)

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
        tests/shader_library_tests.cpp
        tests/render_world_tests.cpp
        tests/mesh_cache_tests.cpp
)
