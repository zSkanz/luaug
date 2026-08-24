// Where a partition is kept between runs (ADR 0053).
//
// **The partitioner runs on play, cached by a hash of the scene.** Not only at
// build: the path somebody uses all day is the one that must not go stale, and
// a build step is a thing to forget. A shipping build pre-warms this same
// directory and is not a second code path -- `luaug build` copies `.luaug/`
// into the artifact, so a cache warmed here travels with the game.
//
// The cache is keyed by the scene's own content hash and validated against the
// stamps the partition read: a scene that names a lamp post has to repartition
// when the lamp post changes, and nothing in the scene's own bytes would say
// so. A cache that answered stale would be a world with yesterday's buildings
// in it, which is the failure the whole thing exists not to have.
//
// It also owns the layering the partitioner cannot: `scene` has no filesystem,
// so it hands cells to a sink and this is the sink.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/scene/partition.h"

#include <filesystem>
#include <string>

namespace luaug::scene {
class World;
}

namespace luaug::app {

struct PartitionOutcome
{
    // False when the project has no scene, the scene could not be read, or the
    // partition produced nothing -- every one of which is an ordinary project
    // rather than an error. Most worlds are small and stream nothing.
    bool active = false;
    // Whether THIS run did the work. False means the cache answered, which is
    // what pressing play a second time is supposed to cost.
    bool repartitioned = false;

    std::filesystem::path directory;
    // The scene to boot: what stayed authored. Empty means the original scene
    // is still the one to load, which is what a project with no cells gets.
    std::filesystem::path scenePath;

    asset::ChunkIndex index;
    scene::PartitionReport report;
};

// Partitions the scene at `scenePath`, or reuses the cache under
// `projectRoot/.luaug/partition`.
//
// `registries` is any world sharing the class, enum and atom tables; it is not
// written to. `contentRoot` is where a stamp the scene names is read from.
// `built` is the index a generated world already occupies, or null -- a record
// landing in one of its cells stays authored, because a `ChunkId` is the
// index's key and two owners of one key is a chunk nothing can reach.
[[nodiscard]] PartitionOutcome partitionProject(scene::World& registries, const std::filesystem::path& projectRoot,
                                                const std::filesystem::path& contentRoot,
                                                const std::filesystem::path& scenePath, const asset::ChunkIndex* built);

} // namespace luaug::app
