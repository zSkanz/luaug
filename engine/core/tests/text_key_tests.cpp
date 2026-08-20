#include "luaug/core/text_key.h"

#include <doctest/doctest.h>

using luaug::core::hashTextKey;
using luaug::core::TextKey;

TEST_CASE("text key hashing")
{
    SUBCASE("is a compile-time constant")
    {
        // The whole point of LUAUG_TR is that a key costs nothing at runtime.
        static_assert(LUAUG_TR("engine.boot.hello").hash != 0);
        constexpr TextKey key = LUAUG_TR("engine.boot.hello");
        CHECK(key.hash == hashTextKey("engine.boot.hello"));
    }

    SUBCASE("matches FNV-1a reference values")
    {
        // Pinned so the hash cannot drift between the engine and the tools
        // that generate the key inventory. These are the published FNV-1a
        // 32-bit results for the given inputs.
        CHECK(hashTextKey("") == 0x811C9DC5u);
        CHECK(hashTextKey("a") == 0xE40C292Cu);
        CHECK(hashTextKey("foobar") == 0xBF9CF968u);
    }

    SUBCASE("distinguishes distinct keys")
    {
        CHECK(hashTextKey("scene.err.parent_cycle") != hashTextKey("scene.err.invalid_size"));
        CHECK(LUAUG_TR("a.b") == LUAUG_TR("a.b"));
        CHECK_FALSE(LUAUG_TR("a.b") == LUAUG_TR("a.c"));
    }
}
