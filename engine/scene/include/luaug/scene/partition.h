// Turning an authored world into a streamed one (ADR 0053).
//
// **A person builds in `Workspace` and presses play.** The grid decides WHEN
// something becomes eligible and the `Model` decides WHAT comes with it; this is
// the arithmetic in between, and there is nothing in it for anybody to
// configure. Walk the scene, take each instance's position, divide by the cell
// size, and write what falls where.
//
// ## The one constraint everything else follows from
//
// **It never holds the world it is reading.** Building every instance in order
// to decide which ones to hold is the exact cost streaming exists to avoid, so
// this reads the scene as TEXT and builds ONE authored node at a time into a
// scratch world -- long enough to ask what that node became, and no longer.
// The peak is a node and its direct children, or one stamp; it is never a
// function of how many instances the scene has.
//
// Building the node rather than interpreting its JSON is what makes the answer
// exact. A scene node's meaning is `readScene`'s to define -- which properties
// override which, what an enum name resolves to, what a stamp expands into --
// and a second reading of it here would be a second definition that disagrees
// the first time either moves. So the node is built by `readSceneNode` and read
// back out of the components, and the world a partition produces is the world
// loading the scene would have produced.
//
// ## What leaves and what stays
//
// Conservative by construction, and counted rather than assumed. An instance
// leaves the scene only when the cell format can express it whole, nothing else
// in the scene points at it, and it carries no descendant a record cannot say.
// Everything else stays authored, and the report says how much of each. A world
// that streams half a weld is worse than a world that streams less.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/core/error.h"
#include "luaug/scene/scene_file.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::scene {

class World;

// Where the size classes are cut, in metres of an object's largest extent
// (ADR 0053): below the first is detail, between them is a structure, above the
// second is a terrain feature.
//
// **Measured rather than chosen.** The flagship's 26,884 instances are 18,496
// ground tiles of 32 m and props between 3.3 m and 6.5 m, with nothing at all
// between 9 m and 32 m. Cuts at 8 and 64 put the ground -- the one thing that
// has to be visible at distance -- in the middle class and left the long-radius
// class empty; these sit in the gap the data actually has.
//
// Fixed rather than configurable, because the ADR's "nothing to configure" is
// about this decision. What a project tunes is the RADIUS each class gets, and
// those are properties on `StreamingService`.
inline constexpr core::f32 StructureExtent = 12.0f;
inline constexpr core::f32 TerrainExtent = 24.0f;

// The size class an object of this extent belongs to, as a `ChunkId::layer`.
[[nodiscard]] core::i32 layerForExtent(core::f32 extent) noexcept;

struct PartitionSettings
{
    core::f32 chunkSize = asset::DefaultChunkSize;

    // Cells a built world already occupies. A record landing in one stays
    // authored instead, because two sources cannot own one cell: a `ChunkId` is
    // the index's key, and a second entry under it would be a chunk the manager
    // can never reach. Rare by construction -- a project either authors its
    // world or generates it -- and counted when it happens rather than silent.
    std::function<bool(asset::ChunkId)> cellTaken;
};

// What one finished cell became on the way out. The partitioner does not know
// where a cell is written or what it is called; the caller does, and answers
// with what the index has to carry.
struct PartitionCellWritten
{
    std::string urn;
    core::u32 bytes = 0;
};

// Called once per finished cell, in `ChunkId` order. The cell is not kept
// afterwards, which is what lets the encoded payloads of a large world never be
// resident together. An empty `urn` drops the cell from the index.
using PartitionSink = std::function<PartitionCellWritten(const asset::Chunk&)>;

// What a partition did. Every count is a thing a person may want explained, and
// "it did not stream" without a reason is the report that sends somebody
// reading the source.
struct PartitionReport
{
    core::u32 cells = 0;
    core::u32 records = 0;
    core::u32 groups = 0;

    // Instances left in the scene, and why. `kept` is the total; the four below
    // are the reasons, and they overlap with nothing.
    core::u32 kept = 0;
    // A `Model` that asked to stay.
    core::u32 persistent = 0;
    // Something else in the scene names it by path, so it has to be findable.
    core::u32 pinned = 0;
    // A cell record cannot say what it is: a light, a script, a part with a
    // child, an attribute, a class that is not `Part` or `MeshPart`.
    core::u32 unstreamable = 0;
    // Its cell belongs to a built world already.
    core::u32 occupied = 0;
    // Stamps the scene names that could not be read.
    core::u32 missingStamps = 0;

    // The high-water mark of instances the scratch world held at once, which is
    // the measurement behind "it never holds the world": it is a property of
    // how the scene is SHAPED -- the widest model, the largest stamp -- and not
    // of how many instances it has.
    core::u32 peakScratchInstances = 0;
};

struct PartitionResult
{
    asset::ChunkIndex index;
    // The scene as it is left: everything that stays authored, spliced from the
    // original text so that nothing which stays is ever rewritten.
    std::string scene;
    PartitionReport report;
};

// Partitions `sceneJson` into cells, handing each finished one to `sink`.
//
// `registries` is any world sharing the class, enum and atom tables the scene is
// about -- the same requirement `StampLibrary` states, for the same reason. It
// is never written to: the scratch world this builds nodes in is its own.
[[nodiscard]] std::optional<core::EngineError> partitionScene(World& registries, std::string_view sceneJson,
                                                              const PartitionSettings& settings,
                                                              const StampSource& stamps, const PartitionSink& sink,
                                                              PartitionResult& out);

} // namespace luaug::scene
