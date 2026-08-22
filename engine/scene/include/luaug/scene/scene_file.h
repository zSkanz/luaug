#pragma once

#include <luaug/core/error.h>
#include <luaug/core/id.h>

#include <optional>
#include <string>
#include <string_view>

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
};

// Serialises the world's authored contents to JSON text.
//
// Deterministic: the same world produces the same bytes, which is what makes a
// scene file diffable and what makes a round-trip test possible at all.
[[nodiscard]] std::string writeScene(const World& world, SceneIoReport* report = nullptr);

// Applies a scene to a world, replacing whatever `Workspace` currently holds.
//
// Replacing rather than merging, because a scene IS the world's contents: a
// load that merged would double every instance the second time it ran, and
// "load a scene" would stop being idempotent.
[[nodiscard]] std::optional<core::EngineError> readScene(World& world, std::string_view json,
                                                         SceneIoReport* report = nullptr);

} // namespace luaug::scene
