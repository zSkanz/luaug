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

if(LUAUG_RHI_SDLGPU AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/sdlgpu")
    message(FATAL_ERROR "LUAUG_RHI_SDLGPU is ON but the backend has not been written yet")
endif()
if(LUAUG_RHI_CAPTURE AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/capture")
    message(FATAL_ERROR "LUAUG_RHI_CAPTURE is ON but the backend has not been written yet")
endif()

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

luaug_add_module_tests(rhi
    MODULES ${luaug_rhi_test_modules}
    SOURCES ${luaug_rhi_test_sources}
)
