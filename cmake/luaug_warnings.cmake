# Warnings-as-errors, applied ONLY to code we author (architecture.md §8).
# Vendored trees are added with SYSTEM so their headers are exempt.
#
# Applied via a target the module helper links, rather than globally, so that
# adding a vendored dependency can never silently relax our own bar.

add_library(luaug_warnings INTERFACE)
add_library(luaug::warnings ALIAS luaug_warnings)

if(MSVC)
    target_compile_options(luaug_warnings INTERFACE
        /W4
        /WX
        /permissive-              # conforming preprocessor + two-phase lookup
        /Zc:__cplusplus           # report the real __cplusplus value
        /Zc:preprocessor
        /w44062                   # unhandled enumerator in a switch
        /w44242 /w44254 /w44263 /w44265 /w44287 /w44289 /w44296 /w44311
        /w44545 /w44546 /w44547 /w44549 /w44555 /w44619 /w44826 /w44928
        /wd4324                   # structure padded due to alignas -- intended
    )
else()
    target_compile_options(luaug_warnings INTERFACE
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wpedantic
        -Wdouble-promotion
        -Wformat=2
    )
endif()
