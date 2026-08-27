# tools/iconpatch -- the application-identity tool named by the M8 scope item.
#
# A tool, not an engine module: plain targets, and it links nothing from
# engine/. Same independence `imgcmp` has, and for a related reason -- this is
# how the packaging step is CHECKED, so a broken engine build must not also take
# away the check.
#
# **The byte layouts are a library and are built everywhere** (S7.10). The tool
# itself is Windows only: a Windows icon IS a PE resource and `UpdateResourceW`
# is the only thing that writes one, where the macOS and Linux equivalents are a
# bundle's `Info.plist` and a `.desktop` entry -- packaging steps for targets v1
# does not build. On any other platform the binary is simply not produced, and
# `luaug build` says so rather than silently shipping the engine's icon.
#
# But the part where a mistake is SILENT is the directory arithmetic, not the
# Win32 call, and that is a parser: it needs no PE and no Windows, so it is
# compiled and tested on both tiers.
add_library(luaug_iconpatch_ico STATIC src/ico.cpp)
target_include_directories(luaug_iconpatch_ico PUBLIC include)
target_link_libraries(luaug_iconpatch_ico PRIVATE luaug::warnings)

if(WIN32)
    add_executable(iconpatch src/main.cpp)
    target_link_libraries(iconpatch PRIVATE luaug::warnings luaug_iconpatch_ico)
endif()

if(LUAUG_BUILD_TESTS)
    add_executable(luaug_iconpatch_tests tests/ico_tests.cpp)
    target_link_libraries(luaug_iconpatch_tests PRIVATE
        luaug::warnings
        doctest::doctest_with_main
        luaug_iconpatch_ico)
    add_test(NAME iconpatch_ico COMMAND luaug_iconpatch_tests)
endif()
