// What terrain is made of, by name and by colour (F1).
//
// **A field stores a `u8` and nothing else**, which is right: the mesher, the
// codec and the world hash all care about the number and none of them care what
// it means. But an editor cannot show a person a number and call it a material
// palette, and a game cannot look one up by name -- so the meaning lives here,
// beside the field rather than inside it.
//
// **Built in rather than authored, for now, and that is a stated limit.** A game
// wants its own list -- `Snow`, `Ash`, `Alien Moss` -- with its own textures, and
// that needs a material asset per entry and a way to point terrain at them,
// which is the same work `BasePart.Material` describes. Until then this is eight
// entries a person can recognise, chosen so that a first world looks like
// somewhere rather than like a grey box.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <span>
#include <string_view>

namespace luaug::asset {

struct TerrainMaterial
{
    core::u8 id = 0;
    std::string_view name;
    // What the mesher tints this material, and what a palette swatch shows.
    // Linear, like every other colour in this engine.
    core::Vec3 color;
};

// **Zero is not in the table.** It means "no ground" everywhere in this system:
// `fillBall` erases with it, `paintBall` refuses it, and the height layer reads
// it as an empty column (D153). Giving it a name would invite a palette entry
// that deletes the world when clicked.
[[nodiscard]] std::span<const TerrainMaterial> terrainPalette() noexcept;

// The entry for an id, or null. Never returns an entry for zero.
[[nodiscard]] const TerrainMaterial* terrainMaterial(core::u8 id) noexcept;

// What an unknown id is drawn as, so a world authored against a bigger palette
// than this build has still renders as something rather than as black.
[[nodiscard]] core::Vec3 terrainColorOf(core::u8 id) noexcept;

} // namespace luaug::asset
