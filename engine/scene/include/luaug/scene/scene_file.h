#pragma once

#include <luaug/core/error.h>
#include <luaug/core/id.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// A world, written down.
//
// **This is what ADR 0047 decided a project's authored world lives in**: the
// scene is the source of truth for what a game starts with, and scripts are
// what it then does. The editor authors this file; boot reads it and then
// starts the scripts.
//
// **It is deliberately the same walk the world hash makes.**
// `engine/scene/src/world_hash.cpp` enumerates every instance in slot order,
// class by name, parent, children in sibling order, attributes, tags, and every
// property of every superclass through the generated accessors -- a complete,
// ordered, deterministic traversal that was written for a different purpose and
// is exactly the traversal a file format needs. Writing a second one would mean
// two definitions of "everything about a world", and they would disagree the
// first time somebody added a property.
//
// Its three warnings are this format's correctness rules, and they are not
// stylistic:
//
//   - **A class is written by NAME, never by `ClassId`.** An id depends on
//     registration order, which is a property of an engine build.
//   - **A name is written as TEXT, never as a `NameAtom`.** An atom is an index
//     into a table this world happens to have interned in this order.
//   - **Nothing is written as struct bytes.** Padding is not a value.
//
// A fourth belongs to a file and not to a hash: **an `InstanceId` is never
// written**. Ids are minted at runtime, so hierarchy is expressed by nesting
// and a reference to another instance is written as a path.
//
// ## What is in scope, and what is not, and why that is stated rather than
// implied
//
// A scene describes **the contents of `Workspace`** and **the properties of the
// services that describe a world** -- `Lighting` today. It does not describe
// services that are the engine's own machinery, it does not contain scripts
// (those are mounted from `src/`, which is ADR 0047's whole point), and it does
// not contain the streamed chunks: those are `luaug-chunk-source`'s job, that
// format is deliberately narrow, and the two meet at streaming and stay two
// things.
//
// ## What a reference can and cannot say
//
// A property whose value is an instance is written as a path from the scene's
// root (`Workspace.Tower.Door`). A reference to something OUTSIDE the scene --
// a service, a streamed chunk, an instance a script made -- cannot be named by
// such a path and is dropped, with a count in the result rather than silently.
// `preserved.h` made the same choice for the same reason and it is worth
// matching: a dangling reference restored as something else is worse than a
// dangling reference restored as nil.

namespace luaug::scene {
class World;

// What a load or a save actually did. Not a bool: a scene that loaded with
// three references dropped and one unknown class is a scene a person should be
// told about, and a caller that only knows "it worked" cannot tell them.
struct SceneIoReport
{
    core::u32 instances = 0;
    core::u32 properties = 0;
    // Instance-valued properties that named something outside the scene.
    core::u32 droppedReferences = 0;
    // Classes the file names that this build does not have. The instance and
    // its whole subtree are skipped -- a `Part` standing in for a class nobody
    // recognises would be a lie shaped like a recovery.
    core::u32 unknownClasses = 0;
    // Properties the file names that the class does not have, or that refused
    // the value. Counted rather than fatal: a scene written by a newer build
    // should still open in an older one, minus what it cannot express.
    core::u32 refusedProperties = 0;
    // Stamped instances placed, and stamps the file names that could not be
    // read (ADR 0049). The second is counted rather than fatal for the same
    // reason as an unknown class: a scene that names a stamp somebody deleted
    // should still open, minus what is gone.
    core::u32 stamped = 0;
    core::u32 missingStamps = 0;
    // Property overrides written or applied: what an instance has of its own
    // (ADR 0051).
    core::u32 overrides = 0;
    // Stamped instances written IN FULL because they no longer have the shape
    // of their stamp -- somebody added or removed something inside one. Counted
    // rather than refused: a save must never lose what is in the world.
    core::u32 unlinkedStamps = 0;
};

// How a scene gets the TEXT of a stamp it names (ADR 0049).
//
// `scene` is L3 and has no filesystem -- it cannot open `content/stamps/`, and
// giving it one would be the layering mistake `architecture.md` §2 exists to
// prevent. The caller knows where stamps live; this is the one question the
// format has to ask it, and a caller that supplies nothing gets a scene whose
// stamped instances are skipped with a count rather than a crash.
using StampSource = std::function<std::optional<std::string>(std::string_view stamp)>;

// The stamps a SAVE needs to read, built once each and kept for the write.
//
// **A stamped instance is written as its mark plus what differs from the stamp**
// (ADR 0051), and "what differs" is a question about two trees -- so the stamp
// has to be built, once, into a world of its own. A world with forty lamp posts
// reads one file, not forty.
//
// Constructed from any world that shares the registries the save is about: a
// `ClassId` is an index into a registry, so the reference tree has to be built
// against the same ones or nothing in it could be compared with anything.
class StampLibrary
{
public:
    StampLibrary(World& registriesFrom, StampSource source);
    ~StampLibrary();

    StampLibrary(const StampLibrary&) = delete;
    StampLibrary& operator=(const StampLibrary&) = delete;

    // Opaque to callers: what a save needs from this is that it exists.
    struct Entry;
    [[nodiscard]] const Entry* reference(const std::string& stamp);

private:
    World& m_registries;
    StampSource m_source;
    std::unordered_map<std::string, std::unique_ptr<Entry>> m_built;
};

// Serialises the world's authored contents to JSON text.
//
// Deterministic: the same world produces the same bytes, which is what makes a
// scene file diffable and what makes a round-trip test possible at all.
// `stamps` is what lets a stamped instance be written as a mark plus its
// overrides. Without one, every stamped instance is written IN FULL and its
// mark dropped -- which loses nothing and is counted, and is what a caller with
// no content root can honestly do.
[[nodiscard]] std::string writeScene(const World& world, SceneIoReport* report = nullptr,
                                     StampLibrary* stamps = nullptr);

// Removes everything a scene describes, leaving the world otherwise intact.
//
// It is `readScene`'s first half, exposed because an editor asking for a NEW
// scene wants exactly that and nothing else. Defining it here rather than in
// the editor is what keeps "what a new scene clears" and "what a save writes"
// the same set -- two definitions of that would disagree the first time either
// moved.
//
// What survives: the services, the `Workspace` itself, and anything marked
// generated. A streamed chunk is not somebody's authored work, and a new scene
// is not a reason to evict the ground.
void clearScene(World& world);

// Applies a scene to a world, replacing whatever `Workspace` currently holds.
//
// Replacing rather than merging, because a scene IS the world's contents: a
// load that merged would double every instance the second time it ran, and
// "load a scene" would stop being idempotent.
[[nodiscard]] std::optional<core::EngineError>
readScene(World& world, std::string_view json, SceneIoReport* report = nullptr, const StampSource& stamps = {});

// Builds ONE scene node -- a plain instance or a stamped one -- under `parent`,
// exactly as `readScene` would build it.
//
// Exposed for the partitioner (ADR 0053), which has to see what a node's
// subtree actually IS and cannot always read that out of the scene: a stamped
// node carries a mark and its overrides, and its parts live in the stamp file.
// The partitioner builds such a node into a scratch world, writes it back out
// in full, and goes on walking text -- so one stamp is resident at a time and
// the expansion is the same code the editor and the loader use, rather than a
// second reading of what a stamp means.
[[nodiscard]] core::InstanceId readSceneNode(World& world, std::string_view nodeJson, core::InstanceId parent,
                                             SceneIoReport* report = nullptr, const StampSource& stamps = {});

// --- Stamps (ADR 0049) -------------------------------------------------------
//
// **A stamp file is a scene of one subtree, and it is the same format.** Same
// writer, same reader, same four correctness rules; the only difference is that
// `root` is one instance instead of `Workspace`. Writing a second format would
// mean two definitions of "everything about a subtree" and they would disagree
// the first time somebody added a property -- which is the argument this file
// already makes about the world hash, one level down.

// Serialises `root` and its subtree as a stamp.
//
// `root`'s own stamp mark is ignored, because this is the file that mark points
// at: a stamp made from an instance of itself would otherwise write a one-line
// file that refers to the file being written.
[[nodiscard]] std::string writeStamp(const World& world, core::InstanceId root, SceneIoReport* report = nullptr,
                                     StampLibrary* stamps = nullptr);

// Instantiates a stamp under `parent` and marks the new root with `stamp`.
//
// Returns the new instance, or an invalid id when the text is not a stamp this
// build can read. Nothing is left half-built on failure: a subtree that could
// not be completed is destroyed rather than parented.
[[nodiscard]] core::InstanceId readStamp(World& world, std::string_view json, core::InstanceId parent,
                                         std::string_view stamp, SceneIoReport* report = nullptr);

// Re-applies a stamp file to every live instance of it, in the subtree at
// `root`. Returns how many were refreshed.
//
// **A stamp is a definition, so changing it changes what already exists** (ADR
// 0051) -- and until this, that was only true across a save and a load. A
// linked instance in a running editor is an ordinary subtree carrying a mark;
// nothing re-read the file while it sat there. This is the same arithmetic done
// live: what each instance has of its own is measured against the file it was
// built from, its children are rebuilt from the file as it stands now, and the
// overrides go back on top.
//
// **`before` is the file's PREVIOUS text and it is not optional.** An instance
// that differs from the new file is either overridden or merely out of date, and
// only the text it was built from tells those two apart.
//
// The instances keep their ids, their parents and their place among their
// siblings, so a reference held to one of them survives -- see the body for what
// that costs and why the alternative costs more. **A subtree whose shape has
// moved on is left alone** and counted in `unlinkedStamps`: it is not an
// instance of that stamp any more, which is the rule the writer already applies.
[[nodiscard]] core::u32 restamp(World& world, core::InstanceId root, std::string_view stamp, std::string_view before,
                                std::string_view after, SceneIoReport* report = nullptr);

} // namespace luaug::scene
