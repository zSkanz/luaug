// Where `asset://…` comes from (architecture.md §2 `asset::mount`).
//
// Three kinds of mount and one lookup:
//
//   * a **directory** of authored source files, which is dev mode -- a
//     `.gltf` on disk, parsed on the way in, hot-reloadable, and exactly the
//     path ADR 0010 keeps forever;
//   * a **pack**, which is what `luaug build-assets` produced -- compiled
//     meshes, transcodable textures, everything named by content hash, and
//     nothing to parse at load;
//   * an **object store**, which is a pack's manifest over loose blob files
//     rather than one archive. The editor writes it when it imports, so that
//     adding one mesh rewrites one file instead of a game's worth of bytes.
//
// The last two answer identically. A caller sees `Source::Pack` from either,
// because "compiled bytes, named by hash" is the same thing to everything
// above this layer, and giving the editor's store its own `Source` would put
// a branch nobody needs into every loader.
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
    enum class Source : core::u8
    {
        // Nothing mounted answers to this URN.
        Missing,
        // Bytes from a pack or an object store, already in the engine's own
        // format.
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

    // The editor's compiled-object store: the same manifest a pack carries --
    // urn, hash, kind -- over one file per blob under `<objects>/<aa>/<hex>`.
    //
    // **A blob is NOT verified at mount, and that difference from `mountPack`
    // is deliberate.** A pack is shipped: it must be whole, or the game says
    // why it will not start. This store is written by the machine reading it
    // and repaired by re-importing, so a missing object is a warning at first
    // use and everything else in the project still opens -- refusing the mount
    // would take a whole project offline over one stale row.
    //
    // Blobs are read on demand and kept, so the lifetime rule above holds
    // literally: a span stays valid while the mount does. `std::unordered_map`
    // never invalidates a reference to an element it already holds, which is
    // what makes that true across later reads rather than only until the next
    // one.
    [[nodiscard]] std::optional<core::EngineError> mountObjects(std::filesystem::path objects,
                                                                const std::filesystem::path& index);

    void clear();

    [[nodiscard]] usize mountCount() const noexcept { return m_mounts.size(); }

    [[nodiscard]] ResolvedContent resolve(std::string_view urn) const;
    [[nodiscard]] bool contains(std::string_view urn) const { return resolve(urn).found(); }

    // By content hash rather than by name, which is what a chunk payload
    // referencing a shared mesh needs. Searches packs and object stores only: a
    // loose file has no hash until something computes one.
    //
    // **Not thread-safe, and this is the call that made that worth writing
    // down.** An object store reads its blob the first time one is asked for,
    // so this is a mutating call wearing a `const`. Resolution happens on the
    // thread that owns the frame -- `MeshLoader::sync` holds a command list,
    // `StreamingHost` resolves once at index time (D039) -- and nothing here
    // pays for a lock it would never contend.
    [[nodiscard]] std::span<const std::byte> blob(const core::ContentHash& hash) const;

    // Every URN a mounted pack or object store names, sorted. For
    // `AssetService` diagnostics and for the streaming manifest.
    [[nodiscard]] std::vector<std::string> packedUrns() const;

    // `<objects>/<aa>/<32 hex digits>`, where `aa` is the first two digits.
    // Fanned out one level because a project's store is tens of thousands of
    // files and a single flat directory is where filesystems and file browsers
    // both stop coping.
    [[nodiscard]] static std::filesystem::path objectPath(const std::filesystem::path& objects,
                                                          const core::ContentHash& hash);

private:
    enum class MountKind : core::u8
    {
        Directory,
        Pack,
        Objects,
    };

    struct Mount
    {
        // Which of the three below is meaningful. Explicit rather than inferred
        // from which field is set: a manifest with no rows leaves `byUrn` empty,
        // and a pack mount that fell through to the directory branch would then
        // resolve URNs against an empty path -- which is the working directory.
        MountKind kind = MountKind::Directory;

        std::filesystem::path directory;
        std::unique_ptr<Pack> pack;
        std::filesystem::path objects;
        std::unordered_map<std::string, PackEntry> byUrn;

        // `Objects` only. Mutable because reading a blob is what a lookup does
        // here; see `blob`. An entry that is present and empty is a file this
        // mount already failed to read, which is what keeps a missing object
        // from being re-opened and re-warned once per frame.
        mutable std::unordered_map<core::ContentHash, std::vector<std::byte>> loaded;
    };

    [[nodiscard]] std::span<const std::byte> objectBytes(const Mount& mount, const core::ContentHash& hash) const;

    std::vector<Mount> m_mounts;
};

} // namespace luaug::asset
