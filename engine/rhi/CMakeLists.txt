# L2 -- the RHI seam and its backends (ADR 0005, ADR 0023).
#
# One directory, several targets. `rhi_api` is header-only and is all any module
# above L2 ever sees; the backends have no public headers at all -- their
# sources live under src/ and are reachable only through the creators in
# luaug/rhi/backends.h. That, not the layer table, is what keeps "everything
# above sees only the interface" true.

luaug_add_module(rhi_api
    INTERFACE
    LAYER 2
    DEPS
        core
)

# Declared on the seam rather than on each backend, because backends.h -- which
# lives in the seam -- is what reads them. A backend that is off is not
# declared, so selecting it fails to compile instead of returning null.
target_compile_definitions(luaug_rhi_api INTERFACE
    LUAUG_RHI_NULL=$<BOOL:${LUAUG_RHI_NULL}>
    LUAUG_RHI_SDLGPU=$<BOOL:${LUAUG_RHI_SDLGPU}>
    LUAUG_RHI_CAPTURE=$<BOOL:${LUAUG_RHI_CAPTURE}>
)


# Accumulated rather than written out, so the test target links exactly the
# backends this configuration built. Naming one unconditionally would turn
# switching a backend off into a confusing CMake error in an unrelated file.
set(luaug_rhi_test_modules rhi_api)

if(LUAUG_RHI_NULL)
    luaug_add_module(rhi_null
        LAYER 2
        SOURCES
            src/null/null_device.cpp
        DEPS
            core
        API_DEPS
            rhi_api
    )
    list(APPEND luaug_rhi_test_modules rhi_null)
endif()

if(LUAUG_RHI_CAPTURE)
    luaug_add_module(rhi_capture
        LAYER 2
        SOURCES
            src/capture/capture_device.cpp
        DEPS
            core
        API_DEPS
            rhi_api
    )
    list(APPEND luaug_rhi_test_modules rhi_capture)
endif()

# The one backend that talks to a driver. It reaches SDL through platform's
# sdl_interop.h -- the access ADR 0004 grants it by name -- and links SDL3
# itself, which is the price that header charges for being included.
#
# Its include root is `interop/`, NOT `include/`. `include/` belongs to the
# seam, so anything that links rhi_api can reach every header in it -- and
# sdlgpu_interop.h, which hands out SDL_GPU handles, must not be reachable that
# way. Its own root means those handles are on the include path only for a
# target that links this backend by name (`app`'s ImGui glue, ADR 0011), while
# rhi_capture and rhi_null go on compiling with no SDL in sight. The seam's own
# headers still arrive here through API_DEPS.
if(LUAUG_RHI_SDLGPU)
    luaug_add_module(rhi_sdlgpu
        LAYER 2
        INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/interop
        SOURCES
            src/sdlgpu/sdlgpu_device.cpp
        DEPS
            core
            platform
        API_DEPS
            rhi_api
        PRIVATE_DEPS
            SDL3::SDL3-static
    )
    list(APPEND luaug_rhi_test_modules rhi_sdlgpu)
endif()

# rhi_api_tests links no backend at all -- if it ever needs one, the seam has
# grown an implementation dependency and the neutrality claim is gone. The
# others come with theirs.
set(luaug_rhi_test_sources tests/rhi_api_tests.cpp)
if(LUAUG_RHI_NULL)
    list(APPEND luaug_rhi_test_sources tests/rhi_null_tests.cpp)
endif()
if(LUAUG_RHI_CAPTURE AND LUAUG_RHI_NULL)
    list(APPEND luaug_rhi_test_sources tests/rhi_capture_tests.cpp)
endif()
if(LUAUG_RHI_SDLGPU)
    list(APPEND luaug_rhi_test_sources tests/rhi_sdlgpu_tests.cpp)
endif()

luaug_add_module_tests(rhi
    MODULES ${luaug_rhi_test_modules}
    SOURCES ${luaug_rhi_test_sources}
)
