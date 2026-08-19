# Sanitizer wiring for the `linux-clang-asan` (and future tsan/msan) presets.
# Sanitizers must reach both compile and link, and must NOT be applied to
# vendored code selectively -- a partially instrumented binary reports
# false positives at the boundary, so this is deliberately global.

if(NOT LUAUG_SANITIZE STREQUAL "")
    if(MSVC)
        # MSVC only ships ASan, and spells it differently.
        if(NOT LUAUG_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only LUAUG_SANITIZE=address (got '${LUAUG_SANITIZE}')")
        endif()
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=${LUAUG_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${LUAUG_SANITIZE})
    endif()
    message(STATUS "LuauG: sanitizers enabled -> ${LUAUG_SANITIZE}")
endif()
