#pragma once

#include <luaug/core/types.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

// A project's assets, as a tree somebody can walk.
//
// **The content directory IS the asset manager** (human decision, 2026-08-22).
// A project's meshes, textures, chunks and **scenes** all live under `content/`,
// a scene is an asset like the rest, and opening one loads it. This is the model
// behind the panel that shows that — Unity's Project window and Unreal's
// Content Browser are the same object.
//
// **Built from the SOURCE tree on disk, not from the packed archive.** What a
// person authors is `content/`; `.luaug/content.lpack` is what a build makes of
// it, and a browser that showed the pack would show yesterday's work and offer
// no way to change it. `ContentMounts` already resolves a loose file over a
// packed one for exactly this reason, and this is the same choice one layer up.
//
// ImGui-free on purpose, like `inspector.h` and `editor.h`: what a panel
// DECIDES is testable and what it draws is a screenshot's business.

namespace luaug::app {

// What an entry is, as far as a browser cares.
//
// Deliberately NOT `asset::AssetKind`. That enum describes what the compiler
// produces and has a `Raw` that means "copied through untouched"; a browser
// describes what a person sees and needs `Folder`, which the pipeline has no
// concept of because a pack is flat.
enum class ContentKind
{
    Folder,
    Scene,
    // A subtree somebody can stamp into the world (ADR 0049). The same format
    // as a scene over a different root, and a kind of its own here because the
    // browser does a different thing with it: a scene is OPENED and a stamp is
    // PLACED.
    Stamp,
    Mesh,
    Texture,
    // What a `Sound` may be pointed at, and what a `TextLabel` may be. Kinds of
    // their own since the property pickers had to answer "which files may this
    // property name" -- a question nothing asked before.
    Audio,
    Font,
    // A material file (`asset/material.h`): the parameter block a `MeshPart`
    // may be pointed at. A kind of its own for the same reason `Audio` is --
    // `MaterialContent`'s picker has to answer "which files may this property
    // name", and "every `.json` in the project" is not that answer.
    Material,
    Chunk,
    Other,
};

struct ContentEntry
{
    std::string name;
    // Relative to the content root, with forward slashes on every platform so a
    // path in a scene file means the same thing on all of them.
    std::string path;
    ContentKind kind = ContentKind::Other;
    // For a stamp, the class of the instance the file is rooted at -- `Model`,
    // `Part`, whatever somebody made it from. Empty for everything else and for
    // a stamp this build could not read.
    //
    // **A stamp's icon is its root's icon**, which is the browser's whole reason
    // for wanting this: a file of a character wears the character's icon and a
    // file of a lamp post wears a part's, and a person recognises their own
    // prefab in a folder of forty without reading a single name.
    std::string rootClass;
    // Bytes, or zero for a folder. Shown rather than used: a person deciding
    // whether a texture is the 4K one wants this and nothing else does.
    core::u64 size = 0;
};

// Which kind a file name is, by extension. `.scene.json` before `.json`,
// because the longer suffix is the specific one and checking the short one
// first would call every scene a plain file.
[[nodiscard]] ContentKind contentKindOf(std::string_view fileName) noexcept;

// The extension a scene carries. One constant, because a browser filters on it,
// a loader checks it and a save appends it — and three copies of a string is how
// two of them end up disagreeing.
inline constexpr std::string_view kSceneExtension = ".scene.json";

// The same, for a stamp (ADR 0049). It pairs with the scene's on purpose: they
// are one format over a different root, and the two names say so.
inline constexpr std::string_view kStampExtension = ".stamp.json";

// Where `Create Stamp...` puts one unless somebody says otherwise. A convention
// rather than a rule -- the file's KIND is in its name, so a project may
// organise them however it likes -- and a default is what stops the first one
// from landing wherever the browser happened to be.
inline constexpr std::string_view kStampFolder = "stamps";

class ContentTree
{
public:
    // Reads the folder at `root / relative`, sorted: folders first, then files,
    // each alphabetically. Sorted rather than left in the order the filesystem
    // answered, because that order is not stable across machines and a browser
    // whose contents move between two people's screens is one they cannot talk
    // to each other about.
    //
    // Returns false if the folder cannot be read, which includes the ordinary
    // case of a project with no `content/` at all.
    bool open(const std::filesystem::path& root, std::string_view relative = {});

    // Re-reads the current folder. Called when something is created, and by the
    // panel's refresh — never per frame: walking a directory sixty times a
    // second is the sort of thing that is invisible until somebody points a
    // project at a network drive.
    bool refresh();

    // Into a subfolder of the current one, or up to its parent. Both are no-ops
    // when there is nowhere to go, so a caller does not have to check first.
    bool enter(std::string_view folderName);
    bool leave();

    [[nodiscard]] const std::vector<ContentEntry>& entries() const noexcept { return m_entries; }
    // Relative to the root, empty at the root itself.
    [[nodiscard]] const std::string& currentFolder() const noexcept { return m_relative; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }
    [[nodiscard]] bool atRoot() const noexcept { return m_relative.empty(); }

    // The absolute path of an entry, for a caller that has to open it.
    [[nodiscard]] std::filesystem::path absolute(const ContentEntry& entry) const;

    // Makes a folder inside the current one and re-reads. False when the name is
    // unusable or the folder already exists — reported rather than thrown,
    // because a person typing a folder name is not an exceptional condition.
    bool createFolder(std::string_view folderName);

    // Renames an entry inside the current folder and re-reads. False when the
    // name is unusable, when something already carries it, or when the rename
    // itself fails.
    //
    // **A file's extension is not the person's to lose.** Renaming
    // `main.scene.json` to `level` produces `level.scene.json`, because the
    // suffix is what makes it a scene and typing a name is not asking to stop
    // being one.
    bool rename(const ContentEntry& entry, std::string_view newName);

    // Removes an entry and re-reads. A folder goes with everything in it, which
    // is what a person means and is why the caller is expected to have asked
    // first.
    bool remove(const ContentEntry& entry);

    // What one import did.
    struct ImportReport
    {
        // Written into the current folder, content-relative, in the order they
        // were given.
        std::vector<std::string> imported;
        // Skipped because something of that name is already here. **Refused
        // rather than overwritten**: an import that replaced a file somebody had
        // already put work into is the one mistake here that costs work, and the
        // browser can say what it skipped.
        std::vector<std::string> skipped;
        // Could not be read or could not be written.
        std::vector<std::string> failed;

        // Brought along because something imported NAMES them: a `.gltf`'s
        // buffer and its images. Counted apart from `imported` so the browser
        // can say "one file, and the six it cannot open without".
        std::vector<std::string> companions;
        // Named by a `.gltf` and not found beside it. The model will not load
        // and this is the only moment anybody can be told why, so it is not
        // folded into `failed` -- nothing failed to copy, something was never
        // there to copy.
        std::vector<std::string> missing;
    };

    // **Copies files from anywhere on the machine into the current folder.**
    //
    // The whole of what importing an asset is in this engine: `content/` holds
    // files, `ContentMounts` resolves a loose one, and a mesh or a texture that
    // is in there is one a project can name. There is no conversion step and no
    // catalogue -- `assetc` compiles the folder when a game is packaged, and
    // what the editor needs is the file.
    //
    // A directory is skipped rather than copied recursively: importing a folder
    // is a different request, and doing it by accident because somebody
    // multi-selected one is not an outcome to design for.
    // **A `.gltf` is a file that names other files**, and importing it without
    // them imports nothing that works: the geometry is in a `.bin` beside it and
    // the textures are in a folder beside that. A person dragging a model in has
    // chosen the model, not a manifest, so the browser reads what it points at
    // and brings that too -- into the same relative subfolder the file expects
    // to find it in, because the URIs inside are relative and rewriting them
    // would be editing somebody's asset.
    //
    // A `.glb` names nothing: it is one file with its buffers inside, and
    // nothing here has to know that -- it simply has no URIs to follow.
    [[nodiscard]] ImportReport import(std::span<const std::filesystem::path> sources);

    // Every file of one kind under the whole root, content-relative and sorted.
    //
    // **The whole tree rather than the folder that is open**, which is the
    // opposite of what this class does everywhere else and is right here: a
    // property picker is asking "which files may this name", and the answer does
    // not depend on where somebody happened to leave the browser.
    //
    // Walked on demand rather than kept: it is read when a picker opens, and a
    // cache would be a second thing to invalidate on every import, rename and
    // delete -- for a directory a project's asset count fits in.
    [[nodiscard]] std::vector<std::string> filesOfKind(ContentKind kind) const;

    // The part of a name before the extension this kind carries, for a rename
    // box to start from: `main` for `main.scene.json`, not `main.scene`.
    [[nodiscard]] static std::string stemOf(const ContentEntry& entry);

    // The name as a person reads it: `lantern-post.stamp`, not
    // `lantern-post.stamp.json`.
    //
    // **`.json` is how the file is STORED and `.stamp` is what it is**, and the
    // browser is the one place that has to say the second rather than the first.
    // Only the trailing `.json` of a compound suffix goes -- a `.json` that is
    // somebody's own data file keeps it, because for that one the storage IS
    // what it is. The name on disk is untouched: this is a label, and every
    // path, rename and load still spells the whole thing.
    [[nodiscard]] static std::string displayNameOf(const ContentEntry& entry);

    // Rejects what a filesystem or a URN cannot carry: empty, a separator, a
    // relative-path segment, or a control character. Exposed so a panel can grey
    // out its own button rather than letting somebody press it and be refused.
    [[nodiscard]] static bool isUsableName(std::string_view name) noexcept;

private:
    std::filesystem::path m_root;
    std::string m_relative;
    std::vector<ContentEntry> m_entries;
};

} // namespace luaug::app
