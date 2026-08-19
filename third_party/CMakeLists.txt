# Vendored dependency wiring (ADR 0021).
#
# Every dependency is added SYSTEM so its headers are exempt from our
# warnings-as-errors bar (architecture.md §8), and with its own tests, tools,
# examples and install rules disabled -- we consume libraries, not projects.
#
# Rows exist in manifest.json for dependencies later milestones need; they are
# wired here only once a module actually links them. Adding an add_subdirectory
# for something nothing uses would slow every build for no benefit.

include(${LUAUG_CMAKE_DIR}/luaug_luau.cmake)

if(LUAUG_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(doctest ${CMAKE_BINARY_DIR}/third_party/doctest SYSTEM)
endif()

# sdl3 is vendored and pinned but not wired: the platform and rhi modules that
# link it arrive in M1 (docs/roadmap.md). Compiling ~2200 files nothing
# references would tax every M0 build for nothing.
