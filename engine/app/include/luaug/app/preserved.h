// `PreserveOnReload` (ADR 0024, api-design.md §3.2, M3 brief Decision 5).
//
// A tagged instance has the same problem as the state bag: the `scene::World`
// it lives in is what a reload destroys. So it is captured as a *description* --
// class, name, ancestry, properties, attributes, tags, and its whole subtree --
// and re-created in the world the reload builds, **before** the new entry
// scripts are deferred. A script that looks for what it left behind therefore
// finds it already there, and `HotReloadService:IsReload()` is how it knows to
// look instead of building it again.
//
// Everything here is text and values rather than ids and atoms: an atom belongs
// to one world's `AtomTable` and an `InstanceId` to one world's slot map, and
// the whole point is that neither world is the other.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <string>
#include <utility>
#include <vector>

namespace luaug::app {

// One instance and its subtree.
struct PreservedInstance
{
    std::string className;
    std::string name;
    // Only what a class declares as writable, which is the same rule
    // `World::clone` follows -- a read-only property is engine-computed and
    // restoring it would be writing a derived value back over its source.
    // `Parent` is excluded here too: it is structure, and the restore sets it.
    std::vector<std::pair<std::string, scene::Value>> properties;
    std::vector<std::pair<std::string, scene::Value>> attributes;
    std::vector<std::string> tags;
    std::vector<PreservedInstance> children;
};

// One step of the chain from `game` down to the preserved instance's parent.
struct PreservedAncestor
{
    std::string className;
    std::string name;
    // A service is fetched or created by class, exactly as `GetService` would.
    // Anything else is found by name and re-created with the class recorded
    // here if the new world does not have it yet -- replayed rather than
    // invented, because the alternative is guessing a class for a Folder that
    // may not have been one.
    bool isService = false;
};

struct PreservedTree
{
    std::vector<PreservedAncestor> path;
    PreservedInstance root;
};

struct PreserveReport
{
    core::u64 captured = 0;
    core::u64 restored = 0;

    // Tagged instances that could not be described: one with no path to `game`,
    // or one mounted under `ScriptService`, whose source is what rebuilds it.
    core::u64 skipped = 0;

    // Instance-valued properties other than `Parent`, which point into a world
    // that is about to stop existing. Zero for the whole v1 surface, where
    // `Parent` is the only one -- counted rather than assumed so that the first
    // class to declare another is a number that moved rather than a silent nil.
    core::u64 droppedReferences = 0;
};

// Everything tagged `PreserveOnReload`, outermost first and each with its
// subtree. A tag on a descendant of an already-tagged instance is redundant and
// is not captured twice.
[[nodiscard]] std::vector<PreservedTree> capturePreserved(const scene::World& world, core::InstanceId dataModel,
                                                          PreserveReport& report);

// Re-creates them under the paths they were captured from. Call after the tree
// exists and before the entry scripts are deferred.
void restorePreserved(scene::World& world, core::InstanceId dataModel, const std::vector<PreservedTree>& trees,
                      PreserveReport& report);

} // namespace luaug::app
