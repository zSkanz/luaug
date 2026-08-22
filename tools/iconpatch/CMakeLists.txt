# tools/iconpatch -- the application-identity tool named by the M8 scope item.
#
# A tool, not an engine module: plain add_executable, and it links nothing from
# engine/. Same independence `imgcmp` has, and for a related reason -- this is
# how the packaging step is CHECKED, so a broken engine build must not also take
# away the check.
#
# Windows only. A Windows icon is a PE resource and `UpdateResourceW` is the
# only thing that writes one; the macOS and Linux equivalents are a bundle's
# `Info.plist` and a `.desktop` entry, both of which belong to packaging steps
# for targets v1 does not build. On any other platform the binary is simply not
# produced, and `luaug build` says so rather than silently shipping the engine's
# icon.
if(WIN32)
    add_executable(iconpatch src/main.cpp)
    target_link_libraries(iconpatch PRIVATE luaug::warnings)
endif()
