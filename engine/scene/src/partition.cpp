#include "luaug/scene/partition.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "json_slice.h"

namespace luaug::scene {
namespace {

using core::u32;
using core::usize;

constexpr std::string_view kFormat = "luaug-scene";

// The seed the scratch world is built with. A constant for the reason
// `scene_file.cpp`'s reference world uses one: nothing in it is simulated,
// nothing reads its generator, and a seed from anywhere else would make a
// partition's output depend on when it ran.
constexpr core::u64 kScratchSeed = 0x5041'5254u;

// One cell, while it is being filled. The string table is interned per cell
// rather than globally: a cell is decoded on its own and its indices have to
// mean something without the rest of the world.
struct Cell
{
    asset::Chunk chunk;
    core::DAABB contents;
    std::unordered_map<std::string, u32> strings;

    [[nodiscard]] u32 intern(std::string_view text)
    {
        if (text.empty()) {
            return asset::ChunkInstance::NoString;
        }
        const std::string key(text);
        if (const auto found = strings.find(key); found != strings.end()) {
            return found->second;
        }
        const auto index = static_cast<u32>(chunk.strings.size());
        chunk.strings.emplace_back(key);
        strings.emplace(key, index);
        return index;
    }
};

// The world-space box a part occupies, rotation included. The absolute-value
// matrix is what turns a rotated box into an axis-aligned one exactly rather
// than by a bounding sphere, and the difference is what the index's own bounds
// are for.
[[nodiscard]] core::DAABB boxOf(const core::CFrameD& cframe, core::Vec3 size) noexcept
{
    const core::f64 hx = std::abs(static_cast<core::f64>(size.x)) * 0.5;
    const core::f64 hy = std::abs(static_cast<core::f64>(size.y)) * 0.5;
    const core::f64 hz = std::abs(static_cast<core::f64>(size.z)) * 0.5;
    const auto& m = cframe.rotation.m;
    const auto absAt = [&m](int column, int row) { return std::abs(static_cast<core::f64>(m[column][row])); };

    // A column is a local axis in world space, so the world extent along an
    // axis is what each local axis contributes to it. Exact for a rotated box,
    // where a bounding sphere would be a third too big in the worst case -- and
    // this is what the index's own bounds are for.
    const core::f64 x = absAt(0, 0) * hx + absAt(1, 0) * hy + absAt(2, 0) * hz;
    const core::f64 y = absAt(0, 1) * hx + absAt(1, 1) * hy + absAt(2, 1) * hz;
    const core::f64 z = absAt(0, 2) * hx + absAt(1, 2) * hy + absAt(2, 2) * hz;
    return core::DAABB::fromMinMax(core::DVec3{cframe.position.x - x, cframe.position.y - y, cframe.position.z - z},
                                   core::DVec3{cframe.position.x + x, cframe.position.y + y, cframe.position.z + z});
}

void expand(core::DAABB& box, const core::DAABB& other) noexcept
{
    box.min.x = std::min(box.min.x, other.min.x);
    box.min.y = std::min(box.min.y, other.min.y);
    box.min.z = std::min(box.min.z, other.min.z);
    box.max.x = std::max(box.max.x, other.max.x);
    box.max.y = std::max(box.max.y, other.max.y);
    box.max.z = std::max(box.max.z, other.max.z);
}

[[nodiscard]] core::f32 extentOf(const core::DAABB& box) noexcept
{
    return static_cast<core::f32>(std::max({box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, 0.0}));
}

[[nodiscard]] core::DVec3 centreOf(const core::DAABB& box) noexcept
{
    return core::DVec3{(box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5, (box.min.z + box.max.z) * 0.5};
}

// The node's own text with its `children` member replaced, or removed when
// `children` is empty. Every other member is copied VERBATIM, which is what
// keeps a scene that partitions to itself byte-identical: no number written by
// the serializer is ever read and written again.
[[nodiscard]] std::string spliceChildren(std::string_view node, std::string_view children)
{
    std::string out;
    out.reserve(node.size() + children.size());
    out.push_back('{');
    bool any = false;
    jsonslice::forEachMember(node, [&](std::string_view key, std::string_view value) {
        if (key == "children") {
            return;
        }
        if (any) {
            out.push_back(',');
        }
        any = true;
        out.push_back('"');
        out.append(key);
        out.append("\":");
        out.append(value);
    });
    if (!children.empty()) {
        if (any) {
            out.push_back(',');
        }
        out.append("\"children\":");
        out.append(children);
    }
    out.push_back('}');
    return out;
}

[[nodiscard]] std::string textOf(std::string_view node, std::string_view key)
{
    const std::optional<std::string_view> slice = jsonslice::member(node, key);
    if (!slice.has_value()) {
        return {};
    }
    const std::optional<std::string> text = jsonslice::unquote(*slice);
    return text.has_value() ? *text : std::string{};
}

// A scratch world that holds one authored node at a time.
//
// It shares the registries it is given, so a `ClassId` and an enum item mean
// the same thing here as in the world the scene is for -- and it is torn down
// after every node, which is what makes the peak a property of the scene's
// SHAPE rather than of its size.
class Scratch
{
public:
    Scratch(World& registries, StampSource stamps)
        : m_world(registries.classes(), registries.enums(), registries.atoms(), kScratchSeed),
          m_stamps(std::move(stamps))
    {}

    [[nodiscard]] World& world() noexcept { return m_world; }
    [[nodiscard]] u32 peak() const noexcept { return m_peak; }

    // Builds one node and returns it, or an invalid id. The caller must call
    // `drop` before building the next: two nodes resident at once is exactly
    // what this class exists to make visible.
    [[nodiscard]] core::InstanceId build(std::string_view nodeJson, SceneIoReport& report)
    {
        const core::InstanceId built = readSceneNode(m_world, nodeJson, core::InstanceId{}, &report, m_stamps);
        m_peak = std::max(m_peak, static_cast<u32>(m_world.instanceCount()));
        return built;
    }

    // **Retired, not merely destroyed, and that is the whole measurement.**
    // `World::destroy` marks a subtree and defers the generation bump to
    // `retireDestroyed`, which the scheduler normally calls at the end of a
    // drain -- and there is no drain here. Without this the scratch world keeps
    // every node the partition ever built, which is exactly the "holds the
    // world" this class exists to make impossible. The change queue goes with
    // them: nothing listens to it here, and a queue nobody drains is the same
    // leak wearing a different name.
    void drop(core::InstanceId id)
    {
        if (id.valid()) {
            (void)m_world.destroy(id);
        }
        m_world.retireDestroyed();
        m_world.changes().clear();
    }

private:
    World m_world;
    StampSource m_stamps;
    u32 m_peak = 0;
};

// What a node did on its way through the grid.
//
// **Three answers rather than two, and D084 is the cost of having had two.** A
// node that STAYS may still have been rewritten, because something under it did
// not stay -- and a parent that only asked "did my children stay" copied itself
// verbatim over the top of that rewrite. Every part of the flagship's `Scenery`
// folder therefore went into a cell AND stayed in the scene, so a partitioned
// world was loaded twice. The copies were coincident, so the picture was right
// and only the counters disagreed.
enum class Outcome
{
    // It went into a cell, and nothing of it is written back.
    Removed,
    // It stays, and its text is the text it arrived as.
    Verbatim,
    // It stays, and something under it did not -- so it is rebuilt.
    Rewritten,
};

// Everything one authored leaf became, read back out of the scratch world.
struct Leaf
{
    bool expressible = false;
    asset::ChunkInstance record;
    std::string name;
    std::string meshContent;
    std::string collisionGroup;
    std::vector<std::string> tags;
    core::DAABB box;
};

class Partitioner
{
public:
    Partitioner(World& registries, const PartitionSettings& settings, const StampSource& stamps,
                const PartitionSink& sink, PartitionResult& out)
        : m_settings(settings), m_sink(sink), m_out(out), m_scratch(registries, stamps),
          m_classes(registries.classes()), m_atoms(registries.atoms())
    {
        m_partClass = m_classes.findId(m_atoms.lookup("Part"));
        m_meshPartClass = m_classes.findId(m_atoms.lookup("MeshPart"));
        m_modelClass = m_classes.findId(m_atoms.lookup("Model"));
    }

    [[nodiscard]] std::optional<core::EngineError> run(std::string_view sceneJson);

private:
    void collectPins(std::string_view node, const std::string& path);
    // Appends the node to `residual` when it stays, and says which of the three
    // things above it did.
    Outcome visit(std::string_view node, const std::string& path, std::string& residual);
    Outcome visitChildren(std::string_view node, const std::string& path, std::string& residual);

    // What a `Model` node is, once it has been built and dropped.
    struct ModelFacts
    {
        bool built = false;
        core::i32 mode = 0;
        // Whether a `ChunkGroup` -- which carries a name and nothing else --
        // could stand for it.
        bool plain = false;
    };
    [[nodiscard]] ModelFacts modelFactsOf(std::string_view node);
    [[nodiscard]] Leaf readLeaf(std::string_view node);
    bool emitLeaf(const Leaf& leaf);
    bool emitGroup(std::string_view node, const std::string& path, bool modelIsPlain);

    [[nodiscard]] Cell& cellFor(asset::ChunkId id);
    void finish();

    const PartitionSettings& m_settings;
    const PartitionSink& m_sink;
    PartitionResult& m_out;
    Scratch m_scratch;
    const ClassRegistry& m_classes;
    const core::AtomTable& m_atoms;

    ClassId m_partClass = InvalidClass;
    ClassId m_meshPartClass = InvalidClass;
    ClassId m_modelClass = InvalidClass;

    std::unordered_set<std::string> m_pins;
    // Ordered, so the index and the order cells are handed to the sink are
    // properties of the world rather than of a hash seed (R10).
    std::map<asset::ChunkId, Cell> m_cells;
    SceneIoReport m_io;
};

// --- pass one: what the scene points at -------------------------------------

void Partitioner::collectPins(std::string_view node, const std::string& path)
{
    const std::string name = textOf(node, "name");
    const std::string here = path.empty() ? name : path + "." + name;

    // An instance-valued property is written as a path from the scene's root,
    // and something the scene names by path has to still be there to be named.
    // The whole ANCESTRY is pinned with it: `Workspace.Tower.Door` cannot
    // resolve if `Tower` has gone.
    const std::string className = textOf(node, "class");
    const ClassId classId = m_classes.findId(m_atoms.lookup(className));
    if (const std::optional<std::string_view> properties = jsonslice::member(node, "properties");
        properties.has_value() && classId != InvalidClass) {
        jsonslice::forEachMember(*properties, [&](std::string_view key, std::string_view value) {
            const PropertyDesc* property = m_classes.findProperty(classId, m_atoms.lookup(key));
            if (property == nullptr || property->type != ValueType::Instance) {
                return;
            }
            const std::optional<std::string> target = jsonslice::unquote(value);
            if (!target.has_value()) {
                return;
            }
            std::string_view walk = *target;
            while (!walk.empty()) {
                m_pins.emplace(walk);
                const usize dot = walk.rfind('.');
                if (dot == std::string_view::npos) {
                    break;
                }
                walk = walk.substr(0, dot);
            }
        });
    }

    if (const std::optional<std::string_view> children = jsonslice::member(node, "children"); children.has_value()) {
        jsonslice::forEachElement(*children, [&](std::string_view child) { collectPins(child, here); });
    }
}

// `Enum.StreamingMode`'s item values, named here so the walk below reads as the
// decision it is. Resolved through the registry rather than assumed, because an
// item's VALUE is the contract and its position in a file is not.
constexpr core::i32 kAtomic = 1;
constexpr core::i32 kPersistent = 2;

// What a `Model` node turns out to be, asked ONCE by building it.
//
// **Read off the component rather than out of the JSON**, which is the same
// rule the leaf below follows and it matters for the same reason: what
// `StreamingMode = "Atomic"` means is `readSceneNode`'s to say -- an enum name
// resolves through the registry, a stamped node's overrides may set it, and a
// second reading here would be a second answer. It also keeps the property
// honest: a field the engine stores and nothing reads is what `inertcheck`
// exists to refuse.
Partitioner::ModelFacts Partitioner::modelFactsOf(std::string_view node)
{
    ModelFacts facts;
    const core::InstanceId model = m_scratch.build(spliceChildren(node, {}), m_io);
    if (!model.valid()) {
        m_scratch.drop(model);
        return facts;
    }
    facts.built = true;

    if (const ModelComponent* component = m_scratch.world().models().find(model); component != nullptr) {
        facts.mode = component->streamingMode;
        // **A group carries a name and nothing else**, so a model with more
        // than a name to it is not one. Attributes and tags have nowhere to go;
        // a pivot offset would move every part under it; and a `PrimaryPart`
        // names a child by path, which pinned that child in the first pass.
        facts.plain = !component->primaryPart.valid();
    }

    AttributeMap attributes;
    m_scratch.world().collectAttributes(model, attributes);
    TagSet tags;
    m_scratch.world().collectTags(model, tags);
    const PVComponent* pv = m_scratch.world().pvInstances().find(model);
    facts.plain =
        facts.plain && attributes.empty() && tags.empty() && (pv == nullptr || pv->pivotOffset == core::CFrameD{});

    m_scratch.drop(model);
    return facts;
}

// --- reading one authored leaf ----------------------------------------------

Leaf Partitioner::readLeaf(std::string_view node)
{
    Leaf leaf;

    // Without its children, because building a subtree to ask about its root is
    // the thing this file exists not to do. The children are visited on their
    // own, and a leaf by definition has none.
    const std::string header = spliceChildren(node, {});
    const core::InstanceId id = m_scratch.build(header, m_io);
    if (!id.valid()) {
        // Nothing was built, and the drop is still called: it is what clears
        // the change queue, and a queue nobody drains grows with the world.
        m_scratch.drop(id);
        return leaf;
    }

    const ClassId classId = m_scratch.world().classOf(id);
    const bool isMesh = classId == m_meshPartClass;
    // Exactly `Part` or `MeshPart`, not merely a `BasePart`: a record names its
    // kind and there is no third thing it can say. And nothing under it -- the
    // node was built without its children, so anything here came from a stamp
    // that expanded into a subtree, which a leaf record cannot hold.
    if ((classId != m_partClass && !isMesh) || m_scratch.world().childCount(id) != 0) {
        m_scratch.drop(id);
        return leaf;
    }

    // An attribute is a value a record has nowhere to put, and dropping it
    // would change the instance on its way through the grid.
    AttributeMap attributes;
    m_scratch.world().collectAttributes(id, attributes);
    if (!attributes.empty()) {
        m_scratch.drop(id);
        return leaf;
    }

    // A pivot a record cannot carry, for the same reason. Identity is what
    // every part that nobody moved the pivot of has, so this almost never
    // fires and is exact when it does.
    if (const PVComponent* pv = m_scratch.world().pvInstances().find(id);
        pv != nullptr && !(pv->pivotOffset == core::CFrameD{})) {
        m_scratch.drop(id);
        return leaf;
    }

    leaf.record.kind = isMesh ? asset::ChunkInstance::Kind::MeshPart : asset::ChunkInstance::Kind::Part;
    if (const PartComponent* part = m_scratch.world().parts().find(id); part != nullptr) {
        leaf.record.cframe = part->cframe;
        leaf.record.size = part->size;
        leaf.record.color = part->color;
        leaf.record.transparency = part->transparency;
        leaf.record.shape = static_cast<core::u8>(part->shape);
        leaf.box = boxOf(part->cframe, part->size);
    }
    if (const RigidBodyComponent* body = m_scratch.world().rigidBodies().find(id); body != nullptr) {
        leaf.record.anchored = body->anchored;
        leaf.record.canCollide = body->canCollide;
        leaf.record.canQuery = body->canQuery;
        leaf.record.friction = body->friction;
        leaf.record.restitution = body->restitution;
        leaf.record.density = body->density;
        leaf.collisionGroup = std::string(m_scratch.world().atoms().text(body->collisionGroup));
    }
    if (isMesh) {
        if (const MeshPartComponent* mesh = m_scratch.world().meshParts().find(id); mesh != nullptr) {
            leaf.meshContent = std::string(m_scratch.world().atoms().text(mesh->meshContent));
            leaf.record.collisionFidelity = static_cast<core::u8>(mesh->collisionFidelity);
        }
    }

    leaf.name = std::string(m_scratch.world().atoms().text(m_scratch.world().name(id)));

    TagSet tags;
    m_scratch.world().collectTags(id, tags);
    leaf.tags.reserve(tags.size());
    for (const core::NameAtom tag : tags) {
        leaf.tags.emplace_back(m_scratch.world().atoms().text(tag));
    }

    m_scratch.drop(id);
    leaf.expressible = true;
    return leaf;
}

// --- filing a record --------------------------------------------------------

Cell& Partitioner::cellFor(asset::ChunkId id)
{
    const auto found = m_cells.find(id);
    if (found != m_cells.end()) {
        return found->second;
    }
    Cell cell;
    cell.chunk.id = id;
    return m_cells.emplace(id, std::move(cell)).first->second;
}

bool Partitioner::emitLeaf(const Leaf& leaf)
{
    const core::i32 layer = layerForExtent(extentOf(leaf.box));
    const asset::ChunkId id = asset::chunkIdAt(leaf.record.cframe.position, m_settings.chunkSize, layer);
    if (m_settings.cellTaken && m_settings.cellTaken(id)) {
        ++m_out.report.occupied;
        return false;
    }

    Cell& cell = cellFor(id);
    if (cell.chunk.instances.size() >= asset::MaxChunkInstances) {
        // A cell is a file with a stated ceiling, and one authored densely
        // enough to reach it keeps what does not fit rather than writing a
        // payload the decoder refuses.
        ++m_out.report.unstreamable;
        return false;
    }

    asset::ChunkInstance record = leaf.record;
    record.name = cell.intern(leaf.name);
    record.meshContent = cell.intern(leaf.meshContent);
    record.collisionGroup = cell.intern(leaf.collisionGroup);
    record.firstTag = static_cast<u32>(cell.chunk.tagRefs.size());
    record.tagCount = static_cast<u32>(leaf.tags.size());
    for (const std::string& tag : leaf.tags) {
        cell.chunk.tagRefs.push_back(cell.intern(tag));
    }
    cell.chunk.instances.push_back(record);
    expand(cell.contents, leaf.box);
    ++m_out.report.records;
    return true;
}

// --- an atomic model --------------------------------------------------------

bool Partitioner::emitGroup(std::string_view node, const std::string& path, bool modelIsPlain)
{
    // Its direct children and nothing deeper. A group is a `Model` and the
    // parts under it -- that is the shape a cell can hold, and it is also the
    // shape it must hold: a record materialises as a child of the group's
    // model, so anything that was not a direct child would come back somewhere
    // else. A model with a deeper tree stays authored instead.
    const std::optional<std::string_view> children = jsonslice::member(node, "children");
    if (!children.has_value() || !modelIsPlain) {
        return false;
    }

    std::vector<Leaf> members;
    core::DAABB box;
    bool ok = true;
    jsonslice::forEachElement(*children, [&](std::string_view child) {
        if (!ok) {
            return;
        }
        const std::string childPath = path + "." + textOf(child, "name");
        if (m_pins.count(childPath) != 0) {
            ok = false;
            return;
        }
        if (const std::optional<std::string_view> grandchildren = jsonslice::member(child, "children");
            grandchildren.has_value()) {
            ok = false;
            return;
        }
        Leaf leaf = readLeaf(child);
        if (!leaf.expressible) {
            ok = false;
            return;
        }
        expand(box, leaf.box);
        members.push_back(std::move(leaf));
    });

    if (!ok || members.empty()) {
        return false;
    }

    // **One cell for the whole model, chosen by where its middle is** -- which
    // is the entire difference `Atomic` buys. A building that overhangs the
    // boundary is filed under one cell and described by a box wider than that
    // cell, which is what the index carries bounds of its own for.
    const core::i32 layer = layerForExtent(extentOf(box));
    const asset::ChunkId id = asset::chunkIdAt(centreOf(box), m_settings.chunkSize, layer);
    if (m_settings.cellTaken && m_settings.cellTaken(id)) {
        ++m_out.report.occupied;
        return false;
    }

    Cell& cell = cellFor(id);
    if (cell.chunk.instances.size() + members.size() > asset::MaxChunkInstances ||
        cell.chunk.groups.size() >= asset::MaxChunkGroups) {
        ++m_out.report.unstreamable;
        return false;
    }

    asset::ChunkGroup group;
    group.name = cell.intern(textOf(node, "name"));
    const auto groupIndex = static_cast<u32>(cell.chunk.groups.size());
    cell.chunk.groups.push_back(group);

    for (const Leaf& member : members) {
        asset::ChunkInstance record = member.record;
        record.group = groupIndex;
        record.name = cell.intern(member.name);
        record.meshContent = cell.intern(member.meshContent);
        record.collisionGroup = cell.intern(member.collisionGroup);
        record.firstTag = static_cast<u32>(cell.chunk.tagRefs.size());
        record.tagCount = static_cast<u32>(member.tags.size());
        for (const std::string& tag : member.tags) {
            cell.chunk.tagRefs.push_back(cell.intern(tag));
        }
        cell.chunk.instances.push_back(record);
        ++m_out.report.records;
    }
    expand(cell.contents, box);
    ++m_out.report.groups;
    return true;
}

// --- pass two: the walk -----------------------------------------------------

Outcome Partitioner::visitChildren(std::string_view node, const std::string& path, std::string& residual)
{
    const std::optional<std::string_view> children = jsonslice::member(node, "children");
    if (!children.has_value()) {
        residual.append(node);
        return Outcome::Verbatim;
    }

    std::string kept = "[";
    bool anyKept = false;
    // **Two questions, not one.** A child that left changes this node; so does a
    // child that stayed and was rebuilt, because the rebuild is in `kept` and
    // nowhere else. Asking only the first is D084: the deeper rewrite was
    // computed, thrown away, and papered over with the original text.
    bool changed = false;
    jsonslice::forEachElement(*children, [&](std::string_view child) {
        std::string one;
        const Outcome outcome = visit(child, path, one);
        if (outcome == Outcome::Removed) {
            changed = true;
            return;
        }
        changed = changed || outcome == Outcome::Rewritten;
        if (anyKept) {
            kept.push_back(',');
        }
        anyKept = true;
        kept.append(one);
    });
    kept.push_back(']');

    // Verbatim when nothing under it moved at all, which is what makes a scene
    // with nothing streamable in it partition to itself byte for byte.
    if (!changed) {
        residual.append(node);
        return Outcome::Verbatim;
    }
    residual.append(spliceChildren(node, anyKept ? kept : std::string_view{}));
    return Outcome::Rewritten;
}

Outcome Partitioner::visit(std::string_view node, const std::string& parentPath, std::string& residual)
{
    const std::string name = textOf(node, "name");
    const std::string path = parentPath.empty() ? name : parentPath + "." + name;
    const bool pinned = m_pins.count(path) != 0;

    // **A stamped node is all or nothing** (ADR 0049 and 0053 together). Its
    // subtree belongs to the stamp file, so streaming part of it would write
    // the rest out in full and unlink every instance of that stamp -- which is
    // a save that loses the link, done silently, by a tool nobody asked to do
    // it. Either the whole of it becomes a group, or it stays with its mark.
    const bool stamped = jsonslice::member(node, "stamp").has_value();

    const std::string className = textOf(node, "class");
    const ClassId classId = stamped ? InvalidClass : m_classes.findId(m_atoms.lookup(className));

    if (pinned) {
        ++m_out.report.pinned;
        ++m_out.report.kept;
        // Pinned means "this instance has to be findable by that path", which
        // says nothing about its children -- so they are still offered the
        // grid.
        return visitChildren(node, path, residual);
    }

    if (stamped) {
        // Expanded into the scratch world so the decision is made against what
        // the stamp actually IS, then written back in full so the same rules
        // read it. One stamp is resident while this runs.
        const core::InstanceId built = m_scratch.build(std::string(node), m_io);
        if (!built.valid()) {
            m_scratch.drop(built);
            ++m_out.report.missingStamps;
            ++m_out.report.kept;
            residual.append(node);
            return Outcome::Verbatim;
        }
        SceneIoReport ignored;
        const std::string expanded = writeStamp(m_scratch.world(), built, &ignored);
        m_scratch.drop(built);

        const std::optional<std::string_view> root = jsonslice::member(expanded, "root");
        if (root.has_value() && m_classes.isA(m_classes.findId(m_atoms.lookup(textOf(*root, "class"))), m_modelClass)) {
            const ModelFacts facts = modelFactsOf(*root);
            if (facts.mode == kAtomic && emitGroup(*root, path, facts.plain)) {
                return Outcome::Removed;
            }
        }
        ++m_out.report.kept;
        ++m_out.report.unstreamable;
        residual.append(node);
        return Outcome::Verbatim;
    }

    if (classId == InvalidClass) {
        // A class this build does not have. Kept whole rather than walked into:
        // `readScene` will skip it and its subtree, and a partition that took
        // parts out of a subtree nothing will build would lose them.
        ++m_out.report.kept;
        ++m_out.report.unstreamable;
        residual.append(node);
        return Outcome::Verbatim;
    }

    if (m_modelClass != InvalidClass && m_classes.isA(classId, m_modelClass)) {
        const ModelFacts facts = modelFactsOf(node);

        if (facts.mode == kPersistent) {
            // `Persistent`: it never enters the grid, and nothing under it does
            // either. It is in the scene, it exists before the first tick, and
            // no eviction reaches it however far the focus walks.
            ++m_out.report.persistent;
            ++m_out.report.kept;
            residual.append(node);
            return Outcome::Verbatim;
        }
        if (facts.mode == kAtomic) {
            if (emitGroup(node, path, facts.plain)) {
                return Outcome::Removed;
            }
            // Asked for whole and cannot be given whole, so it is not quietly
            // demoted to `Nonatomic`: half a mechanism is what `Atomic` exists
            // to prevent, and taking its parts out one at a time is exactly
            // that.
            ++m_out.report.kept;
            ++m_out.report.unstreamable;
            residual.append(node);
            return Outcome::Verbatim;
        }
        // `Nonatomic`: the model stays and its parts descend individually, so a
        // path to the model still resolves and a script that looks for it finds
        // it -- empty, until the grid brings its parts back.
        ++m_out.report.kept;
        return visitChildren(node, path, residual);
    }

    const bool leafShaped = !jsonslice::member(node, "children").has_value();
    if (leafShaped && (classId == m_partClass || classId == m_meshPartClass)) {
        const Leaf leaf = readLeaf(node);
        if (leaf.expressible && emitLeaf(leaf)) {
            return Outcome::Removed;
        }
        ++m_out.report.kept;
        ++m_out.report.unstreamable;
        residual.append(node);
        return Outcome::Verbatim;
    }

    ++m_out.report.kept;
    return visitChildren(node, path, residual);
}

void Partitioner::finish()
{
    for (auto& entry : m_cells) {
        Cell& cell = entry.second;
        if (cell.chunk.instances.empty()) {
            continue;
        }

        // The footprint UNION the contents, never the contents alone. A cell is
        // never further away than its own square -- which is what M7's index
        // meant and what every world built before this one is scored by -- and
        // an atomic model that hangs over the edge widens it.
        cell.chunk.bounds = asset::chunkBounds(cell.chunk.id, m_settings.chunkSize);
        cell.chunk.bounds.min.y = cell.contents.min.y;
        cell.chunk.bounds.max.y = cell.contents.max.y;
        expand(cell.chunk.bounds, cell.contents);

        const PartitionCellWritten written = m_sink ? m_sink(cell.chunk) : PartitionCellWritten{};
        if (written.urn.empty()) {
            continue;
        }

        asset::ChunkIndexEntry indexEntry;
        indexEntry.id = cell.chunk.id;
        indexEntry.bounds = cell.chunk.bounds;
        indexEntry.urn = written.urn;
        indexEntry.instanceCount = static_cast<u32>(cell.chunk.instances.size());
        indexEntry.bytes = written.bytes;
        m_out.index.chunks.push_back(std::move(indexEntry));
        ++m_out.report.cells;
    }
    m_out.index.chunkSize = m_settings.chunkSize;
    m_out.report.peakScratchInstances = m_scratch.peak();
}

std::optional<core::EngineError> Partitioner::run(std::string_view sceneJson)
{
    const std::optional<std::string_view> root = jsonslice::member(sceneJson, "root");
    if (textOf(sceneJson, "format") != kFormat) {
        return core::makeError(LUAUG_TR("scene.err.scene_format"));
    }
    if (!root.has_value()) {
        return core::makeError(LUAUG_TR("scene.err.scene_parse"), {}, "the scene has no root");
    }

    collectPins(*root, {});

    std::string residual;
    residual.reserve(sceneJson.size());
    residual.append("{\"format\":\"");
    residual.append(kFormat);
    residual.append("\",\"version\":1,\"root\":");
    (void)visitChildren(*root, textOf(*root, "name"), residual);
    residual.push_back('}');

    m_out.scene = std::move(residual);
    finish();
    return std::nullopt;
}

} // namespace

core::i32 layerForExtent(core::f32 extent) noexcept
{
    if (extent >= TerrainExtent) {
        return 2;
    }
    return extent >= StructureExtent ? 1 : 0;
}

std::optional<core::EngineError> partitionScene(World& registries, std::string_view sceneJson,
                                                const PartitionSettings& settings, const StampSource& stamps,
                                                const PartitionSink& sink, PartitionResult& out)
{
    out = PartitionResult{};
    Partitioner partitioner(registries, settings, stamps, sink, out);
    return partitioner.run(sceneJson);
}

} // namespace luaug::scene
