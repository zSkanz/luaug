// Where `asset://…` comes from (architecture.md §2 `asset::mount`).
//
// Two kinds of mount and one lookup:
//
//   * a **directory** of authored source files, which is dev mode -- a
//     `.gltf` on disk, parsed on the way in, hot-reloadable, and exactly the
//     path ADR 0010 keeps forever;
//   * a **pack**, which is what `luaug build-assets` produced -- compiled
//     meshes, transcodable textures, everything named by content hash, and
//     nothing to parse at load.
//
// A URN resolves through the mounts in reverse order, so **a later mount wins**
// and a project can override engine content by mounting after it. That rule is
// stated because the alternative -- first wins -- makes overriding impossible
// and looks identical until somebody tries.
//
// **This layer never decodes anything.** It answers "where are the bytes", and
// `mesh_format`, `texture` and `gltf` answer what they mean. Keeping that split
// is what lets the streaming manager ask for a chunk without knowing what a
// chunk contains.
#pragma once

#include "luaug/asset/pack.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luaug::asset {

// `asset://models/tree.glb`. The one scheme, and the only one v1 has.
inline constexpr std::string_view AssetScheme = "asset://";

// True for a URN this engine will resolve. A path that escapes the mount --
// `asset://../../etc/passwd` -- is refused here rather than at the filesystem,
// because a shipped game mounting a pack has no filesystem to refuse it.
[[nodiscard]] bool isValidUrn(std::string_view urn);

// Strips the scheme. Empty for anything `isValidUrn` refuses.
[[nodiscard]] std::string_view urnPath(std::string_view urn);

struct ResolvedContent
{
    enum class Source : core::u8 {
        // Nothing mounted answers to this URN.
        Missing,
        // Bytes inside a mounted pack, already in the engine's own format.
        Pack,
        // A file on disk, in whatever the author saved it as.
        Loose,
    };

    Source source = Source::Missing;
    AssetKind kind = AssetKind::Unknown;
    // Set for `Pack`. A view into the mount's storage: valid while the mount is.
    std::span<const std::byte> bytes;
    // Set for `Loose`.
    std::filesystem::path path;
    // Set for `Pack`; zero for `Loose`, because a loose file has no name of
    // this kind until something hashes it.
    core::ContentHash hash;

    [[nodiscard]] bool found() const noexcept { return source != Source::Missing; }
};

class ContentMounts
{
public:
    // Later mounts win. Adding the same directory twice is two mounts, which is
    // harmless and is not worth a check nobody would hit.
    void mountDirectory(std::filesystem::path root);

    // Reads `<pack>` and the manifest beside it (`<pack-stem>.manifest.json`),
    // or an explicit manifest path. Both must agree: a manifest naming a hash
    // the pack does not hold is refused at mount rather than at first use,
    // because a game that starts and then cannot find its world is worse than
    // one that says why it will not start.
    [[nodiscard]] std::optional<core::EngineError> mountPack(const std::filesystem::path& pack,
                                                             const std::filesystem::path& manifest = {});

    void clear();

    [[nodiscard]] usize mountCount() const noexcept { return m_mounts.size(); }

    [[nodiscard]] ResolvedContent resolve(std::string_view urn) const;
    [[nodiscard]] bool contains(std::string_view urn) const { return resolve(urn).found(); }

    // By content hash rather than by name, which is what a chunk payload
    // referencing a shared mesh needs. Searches packs only: a loose file has no
    // hash until something computes one.
    [[nodiscard]] std::span<const std::byte> blob(const core::ContentHash& hash) const;

    // Every URN a mounted pack names, sorted. For `AssetService` diagnostics and
    // for the streaming manifest.
    [[nodiscard]] std::vector<std::string> packedUrns() const;

private:
    struct Mount
    {
        // Exactly one of these is set.
        std::filesystem::path directory;
        std::unique_ptr<Pack> pack;
        std::unordered_map<std::string, PackEntry> byUrn;
    };

    std::vector<Mount> m_mounts;
};

} // namespace luaug::asset
