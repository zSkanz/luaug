// Compiling what an import brought in, and only in a build that has an editor.
//
// **`content/` still holds the files, and that has not changed.** An import
// copies a `.gltf` and its companions into the project the way it always has;
// this is what happens next, and what happens next is that the same compiler
// `assetc` is runs over exactly those files and writes what it produced into the
// project's own object store (E9 step 12).
//
// Why the store and not a pack: a `.lpack` is one file holding everything, so
// adding to it means rewriting it -- the wrong shape for a tool where somebody
// drops one model into a folder and expects the other forty to still be there.
// A store is one file per blob plus an index, so an import appends.
//
// **This header exists so that `assetc` is named in one place.** It carries the
// basis encoder and assimp, which is exactly what `asset/texture.h` split the
// encode and the transcode apart to keep out of a shipped game -- so the whole
// unit compiles only under `LUAUG_DEBUG_UI`, the flag ImGui and SDL3 are already
// linked behind, and a shipping build carries no encoder at all.
#pragma once

#include "luaug/core/types.h"

#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace luaug::app {

// What compiling one import produced.
struct ContentImportReport
{
    // Content-relative names that compiled, in the order they were given.
    std::vector<std::string> compiled;
    // Named, tried, and refused -- with the compiler's own diagnostic, because
    // "it did not import" is a sentence nobody can act on.
    std::vector<std::string> failed;
    std::string diagnostic;

    core::u32 meshes = 0;
    core::u32 textures = 0;

    // **Whether any work actually happened.** Counts above are reported the same
    // on a cache hit as on a miss -- deliberately, so a build's totals do not
    // depend on what was cached -- which makes them useless for answering "did
    // opening this project do anything". These answer it, and they are what the
    // message at the call site is written against: a re-open that compiled
    // nothing should say nothing.
    core::u32 cacheHits = 0;
    core::u32 cacheMisses = 0;

    // What each compiled model split into, by content-relative name: the URN
    // FRAGMENTS, in the order the compiler produced them.
    //
    // **This is what lets the editor place a `Model` of named parts** rather
    // than one opaque `MeshPart` (E9 step 12). The names come from the compiler
    // rather than from a second walk of the file, so the instance's name, the
    // fragment in its `MeshContent` and the blob in the store are one answer
    // instead of three that have to agree.
    //
    // A file that produced one piece is absent: one piece is the whole model,
    // and there is nothing to split.
    std::vector<std::pair<std::string, std::vector<std::string>>> pieces;
};

// Compiles `names` -- content-relative paths under `contentRoot` -- into the
// object store under `projectRoot / ".luaug" / "import"`.
//
// **Every name goes through `assetc::importOne`**, which is the same call a
// command-line build makes, so the blobs are the same bytes rather than bytes
// two implementations agree about. A name whose kind the compiler has nothing to
// do with -- a script, a scene -- is skipped rather than failed: it is content
// the project reads directly and there is nothing to compile.
//
// Returns an empty report and no error in a build with no editor.
[[nodiscard]] ContentImportReport compileImported(const std::filesystem::path& projectRoot,
                                                  const std::filesystem::path& contentRoot,
                                                  std::span<const std::string> names);

// Where the store lives, so the mount at boot and the writer at import cannot
// disagree about it. `<project>/.luaug/import/objects` and `.../index.json`.
[[nodiscard]] std::filesystem::path importObjectsDir(const std::filesystem::path& projectRoot);
[[nodiscard]] std::filesystem::path importIndexPath(const std::filesystem::path& projectRoot);

} // namespace luaug::app
