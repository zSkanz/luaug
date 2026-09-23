// The scene's tree as types (ADR 0078): what `workspace` holds, so that
// `workspace.Player.Walker` is a `CharacterBody` to the analyzer and a typo in
// the path is still an error.
//
// **The same idea a source map gives a language server**: the engine declares
// every class, and only the project knows its own tree. So the tree is written
// down as a second definitions file, `.luaug/types/scene.d.luau`, loaded after
// `engine.d.luau`. Its `declare workspace:` replaces the plain one -- the later
// declaration wins -- with `Workspace & { ... }`, an intersection the analyzer
// reads child by child.
//
// Rewritten by the editor on every save and when it opens a project, and by
// `luaug-host <project> --write-types`, which `luaug setup` and `luaug check`
// run. A child a script creates at run time has no declared name and is not in
// it; `FindFirstChild` or a cast reaches that one.
#pragma once

#include "luaug/core/id.h"

#include <filesystem>
#include <string>

namespace luaug::scene {
class World;
}

namespace luaug::app {

// The definitions text for `world`'s workspace. A child whose name is a member
// of its parent's class is left out, because the member wins at run time; of
// two children with one name, the first is declared, because the first is what
// a dot reaches. Streamed-in instances are not the scene's and are left out.
[[nodiscard]] std::string sceneDefinitions(const scene::World& world);

// Writes it to `<project>/.luaug/types/scene.d.luau`. False when the file could
// not be written.
bool writeSceneDefinitions(const scene::World& world, const std::filesystem::path& project);

} // namespace luaug::app
