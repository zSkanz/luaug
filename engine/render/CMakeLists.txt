# L4 -- architecture.md §2 eventually gives render core, jobs, platform,
# rhi_api, asset and scene. At M1 it has CPU-side geometry generation, the
# shader library and the debug pass, so it needs core and the RHI seam; the rest
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
    DEPS
        core
        rhi_api
)

luaug_add_module_tests(render
    SOURCES
        tests/debug_draw_tests.cpp
        tests/shader_library_tests.cpp
)
