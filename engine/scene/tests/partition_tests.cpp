// The partitioner (ADR 0053).
//
// **The oracle is the world, not the cells.** A partition is correct when the
// world you get from loading the residual scene and materialising every cell is
// the world you would have got from loading the scene whole -- same parts, same
// places, same properties, same tags. Asserting on cell counts alone would pass
// a partitioner that dropped a colour.
//
// These build against the REAL registry rather than the hand-written fixture
// beside them. The partitioner reads a scene node by BUILDING it, so what it
// can express is decided by the generated accessors and the component hooks;
// a fixture that mirrored them would be asserting a copy of the thing under
// test.
#include "luaug/asset/chunk.h"
#include "luaug/asset/field_cells.h"
#include "luaug/core/base64.h"
#include "luaug/core/i18n.h"
#include "luaug/scene/components.h"
#include "luaug/scene/partition.h"
#include "luaug/scene/scene_file.h"
#include "luaug/scene/streaming_glue.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <map>
#include <string>
#include <vector>

#include "../generated/class_descriptors.gen.h"

using namespace luaug;
using luaug::scene::PartitionResult;
using luaug::scene::PartitionSettings;

namespace {

void seedRealCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A world over the shipped registry, with a `Workspace` in it -- which is what a
// scene is rooted at and what `readScene` refuses to run without.
struct Registries
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;

    Registries()
    {
        scene::generated::registerEnums(enums, atoms);
        scene::generated::registerClasses(classes, atoms);
    }

    Registries(const Registries&) = delete;
    Registries& operator=(const Registries&) = delete;
};

struct Sandbox
{
    Registries registries;
    scene::World world{registries.classes, registries.enums, registries.atoms, 7u};
    core::InstanceId workspace;

    Sandbox()
    {
        workspace = world.create(registries.classes.findId(registries.atoms.lookup("Workspace")));
        world.setName(workspace, registries.atoms.intern("Workspace"));
    }
};

// A scene in the canonical compact shape the serializer writes, so that a
// residual which changed nothing is comparable to it byte for byte.
[[nodiscard]] std::string sceneText(const std::string& children, const std::string& rootProperties = {})
{
    std::string text = R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace","name":"Workspace")";
    if (!rootProperties.empty()) {
        text += ",\"properties\":" + rootProperties;
    }
    text += ",\"children\":[" + children + "]}}";
    return text;
}

[[nodiscard]] std::string cframe(double x, double y, double z)
{
    return "[" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ",1,0,0,0,1,0,0,0,1]";
}

[[nodiscard]] std::string partNode(const std::string& name, double x, double y, double z, double size = 2.0,
                                   const std::string& extra = {})
{
    const std::string sizeText =
        "[" + std::to_string(size) + "," + std::to_string(size) + "," + std::to_string(size) + "]";
    return R"({"class":"Part","name":")" + name + R"(","properties":{"CFrame":)" + cframe(x, y, z) + R"(,"Size":)" +
           sizeText + R"(,"Anchored":true})" + extra + "}";
}

[[nodiscard]] std::string partNodeWith(const std::string& name, double x, double y, double z, double size,
                                       const std::string& leadingProperties, const std::string& extra)
{
    const std::string sizeText =
        "[" + std::to_string(size) + "," + std::to_string(size) + "," + std::to_string(size) + "]";
    return R"({"class":"Part","name":")" + name + R"(","properties":{)" + leadingProperties + R"("CFrame":)" +
           cframe(x, y, z) + R"(,"Size":)" + sizeText + R"(,"Anchored":true})" + extra + "}";
}

struct Partition
{
    PartitionResult result;
    // Every cell the sink was handed, keyed by its URN so a test can name one.
    std::map<std::string, asset::Chunk> cells;
};

[[nodiscard]] Partition partition(Sandbox& sandbox, const std::string& text, const PartitionSettings& settings = {})
{
    Partition out;
    const scene::PartitionSink sink = [&out](const asset::Chunk& cell) {
        scene::PartitionCellWritten written;
        written.urn =
            "cell_" + std::to_string(cell.id.x) + "_" + std::to_string(cell.id.z) + "_" + std::to_string(cell.id.layer);
        if (cell.id.y != 0)
            written.urn += "_y" + std::to_string(cell.id.y);
        written.bytes = static_cast<core::u32>(asset::encodeChunk(cell).size());
        out.cells.emplace(written.urn, cell);
        return written;
    };
    const std::optional<core::EngineError> error =
        scene::partitionScene(sandbox.world, text, settings, {}, sink, out.result);
    REQUIRE(!error.has_value());
    return out;
}

// Every part in a world, as a comparable row. The tree SHAPE differs by design
// -- streaming parents into a chunk folder -- so what has to match is the set
// of parts and what each of them is.
struct PartRow
{
    std::string name;
    core::DVec3 position;
    core::Vec3 size;
    core::Color3 color;
    bool anchored = false;
    bool canCollide = true;
    core::f32 friction = 0.0f;
    core::f32 density = 0.0f;
    std::string collisionGroup;
    std::string tags;

    [[nodiscard]] bool operator<(const PartRow& other) const
    {
        if (name != other.name)
            return name < other.name;
        if (position.x != other.position.x)
            return position.x < other.position.x;
        if (position.y != other.position.y)
            return position.y < other.position.y;
        return position.z < other.position.z;
    }
};

[[nodiscard]] std::vector<PartRow> partsOf(const scene::World& world, core::InstanceId root)
{
    std::vector<core::InstanceId> descendants;
    world.collectDescendants(root, descendants);

    std::vector<PartRow> rows;
    for (const core::InstanceId id : descendants) {
        const scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr)
            continue;
        PartRow row;
        row.name = std::string(world.atoms().text(world.name(id)));
        row.position = part->cframe.position;
        row.size = part->size;
        // The surface as the grid carries it: the default material's colour.
        row.color = part->materialParameters.has(asset::MaterialField::Color) ? part->materialParameters.color
                                                                              : core::Color3{1.0f, 1.0f, 1.0f};
        if (const scene::RigidBodyComponent* body = world.rigidBodies().find(id); body != nullptr) {
            row.anchored = body->anchored;
            row.canCollide = body->canCollide;
            row.friction = body->friction;
            row.density = body->density;
            row.collisionGroup = std::string(world.atoms().text(body->collisionGroup));
        }
        scene::TagSet tags;
        world.collectTags(id, tags);
        std::vector<std::string> named;
        for (const core::NameAtom tag : tags)
            named.emplace_back(world.atoms().text(tag));
        std::sort(named.begin(), named.end());
        for (const std::string& tag : named)
            row.tags += tag + ";";
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

} // namespace

TEST_CASE("the size classes are cut where the data has a gap")
{
    // Measured against the flagship: nothing lives between the largest prop at
    // 9 m and the ground at 32 m, so the cuts sit in the gap and all three
    // classes do something.
    CHECK(scene::layerForExtent(3.3f) == 0);
    CHECK(scene::layerForExtent(6.5f) == 0);
    CHECK(scene::layerForExtent(11.9f) == 0);
    CHECK(scene::layerForExtent(12.0f) == 1);
    CHECK(scene::layerForExtent(23.9f) == 1);
    CHECK(scene::layerForExtent(24.0f) == 2);
    CHECK(scene::layerForExtent(32.0f) == 2);
}

TEST_CASE("a scene with nothing streamable partitions to itself, byte for byte")
{
    seedRealCatalog();
    Sandbox sandbox;

    // A camera, a script and a part with a child: none of the three is a leaf
    // the cell format can say, so nothing leaves and the residual is the input.
    const std::string children = R"({"class":"Camera","name":"MainCamera"},)"
                                 R"({"class":"Script","name":"Boot"},)"
                                 R"({"class":"Part","name":"Post","children":[{"class":"PointLight","name":"Lamp"}]})";
    const std::string text = sceneText(children);

    const Partition partitioned = partition(sandbox, text);
    CHECK(partitioned.result.scene == text);
    CHECK(partitioned.result.report.records == 0);
    CHECK(partitioned.cells.empty());
}

TEST_CASE("the block world and every other top-level member survive a partition")
{
    // **A defect, found reading the partitioner for terrain streaming**: the
    // residual scene was rebuilt from `format`, `version` and `root`, and the
    // block world is a top-level `voxels` member -- so a game with blocks and
    // enough parts to partition booted with no blocks at all.
    seedRealCatalog();
    Sandbox sandbox;

    const std::string voxels =
        R"({"blockSize":1,"types":[{"name":"Stone"}],"chunks":[{"x":0,"y":0,"z":0,"blocks":"AQA="}]})";
    std::string text = sceneText(partNode("Near", 10.0, 0.0, 10.0));
    text.pop_back();
    text += R"(,"voxels":)" + voxels + "}";

    const Partition partitioned = partition(sandbox, text);
    CHECK(partitioned.result.report.records == 1);
    CHECK(partitioned.result.scene.find(R"("voxels":)" + voxels) != std::string::npos);
    CHECK(partitioned.result.scene.find("\"Near\"") == std::string::npos);
}

TEST_CASE("a loose part goes into the cell its position falls in")
{
    seedRealCatalog();
    Sandbox sandbox;

    const std::string text = sceneText(partNode("Near", 10.0, 0.0, 10.0) + "," + partNode("Far", 300.0, 0.0, -20.0));
    const Partition partitioned = partition(sandbox, text);

    CHECK(partitioned.result.report.records == 2);
    CHECK(partitioned.cells.size() == 2);
    CHECK(partitioned.cells.count("cell_0_0_0") == 1);
    // -20 is on the negative side of the origin, which is the case a truncating
    // cast gets wrong and a floor gets right.
    CHECK(partitioned.cells.count("cell_1_-1_0") == 1);
    CHECK(partitioned.result.scene.find("\"Near\"") == std::string::npos);
}

TEST_CASE("a cave under the ground is a cell of its own, and each is only as tall as what it holds (ADR 0086)")
{
    // **The owner's mandate, S4**: a player in a cave loaded the whole column
    // above them, because a cell was a column and its box ran from the cave to
    // the ground. Cut into bands, the two are two cells, and the surface is
    // four hundred metres from anybody standing in the cave.
    seedRealCatalog();
    Sandbox sandbox;

    const std::string text =
        sceneText(partNode("Hut", 10.0, 1.0, 10.0) + "," + partNode("Crystal", 12.0, -400.0, 14.0));
    const Partition partitioned = partition(sandbox, text);

    REQUIRE(partitioned.cells.size() == 2);
    REQUIRE(partitioned.cells.count("cell_0_0_0") == 1);
    REQUIRE(partitioned.cells.count("cell_0_0_0_y-2") == 1);
    const asset::Chunk& surface = partitioned.cells.at("cell_0_0_0");
    const asset::Chunk& cave = partitioned.cells.at("cell_0_0_0_y-2");
    CHECK(surface.bounds.min.y > -1.0);
    CHECK(cave.bounds.max.y < -398.0);

    // What the streaming manager scores a cell by: a focus in the cave is on
    // top of the cave's cell and a long way from the surface's.
    const core::DVec3 inCave{12.0, -399.0, 14.0};
    CHECK(core::distanceSquared(cave.bounds, inCave) == doctest::Approx(0.0));
    CHECK(core::distanceSquared(surface.bounds, inCave) > 390.0 * 390.0);
    // And the index names both, apart.
    CHECK(partitioned.result.index.find(asset::ChunkId{0, 0, 0}) != nullptr);
    CHECK(partitioned.result.index.find(asset::ChunkId{0, 0, 0, -2}) != nullptr);
}

TEST_CASE("something the scene points at stays where it can be pointed at")
{
    seedRealCatalog();
    Sandbox sandbox;

    // `Workspace.CurrentCamera` is an instance-valued property written as a
    // path. The part it names cannot leave, or the path resolves to nothing.
    const std::string children = partNode("Anchor", 4.0, 0.0, 4.0) + "," +
                                 R"({"class":"Weld","name":"Hold","properties":{"Part0":"Workspace.Anchor"}})";
    const Partition partitioned = partition(sandbox, sceneText(children));

    CHECK(partitioned.result.report.records == 0);
    CHECK(partitioned.result.report.pinned == 1);
    CHECK(partitioned.result.scene.find("\"Anchor\"") != std::string::npos);
}

TEST_CASE("a persistent model is never in a cell, four kilometres out or anywhere else")
{
    seedRealCatalog();
    Sandbox sandbox;

    const std::string beacon = R"({"class":"Model","name":"Beacon","properties":{"StreamingMode":"Persistent"},)"
                               R"("children":[)" +
                               partNode("Mast", 4000.0, 20.0, 4000.0) + "]}";
    const Partition partitioned = partition(sandbox, sceneText(beacon + "," + partNode("Loose", 4010.0, 0.0, 4010.0)));

    // The loose part four kilometres out DOES go into a cell -- distance is not
    // what keeps the beacon out, the mode is.
    CHECK(partitioned.result.report.records == 1);
    CHECK(partitioned.result.report.persistent == 1);
    CHECK(partitioned.result.scene.find("\"Mast\"") != std::string::npos);
    for (const auto& cell : partitioned.cells) {
        for (const asset::ChunkInstance& record : cell.second.instances) {
            CHECK(cell.second.stringAt(record.name) != "Mast");
        }
    }
}

TEST_CASE("an atomic model crosses a boundary and is filed under one cell")
{
    seedRealCatalog();
    Sandbox sandbox;

    // The piers sit either side of x = 256, so a per-part partition would put
    // them in two cells and the gate would arrive in halves.
    const std::string gate = R"({"class":"Model","name":"Gatehouse","properties":{"StreamingMode":"Atomic"},)"
                             R"("children":[)" +
                             partNode("West", 250.0, 5.0, 300.0, 6.0) + "," + partNode("East", 262.0, 5.0, 300.0, 6.0) +
                             "," + partNode("Lintel", 256.0, 11.0, 300.0, 6.0) + "]}";
    const Partition partitioned = partition(sandbox, sceneText(gate));

    REQUIRE(partitioned.cells.size() == 1);
    const asset::Chunk& cell = partitioned.cells.begin()->second;
    CHECK(cell.instances.size() == 3);
    CHECK(cell.groups.size() == 1);
    CHECK(cell.stringAt(cell.groups[0].name) == "Gatehouse");
    for (const asset::ChunkInstance& record : cell.instances) {
        CHECK(record.group == 0);
    }
    // And the cell's box is WIDER than its own square, which is what the index
    // has bounds of its own for: the model hangs over the edge it was filed on.
    CHECK(cell.bounds.min.x < static_cast<core::f64>(cell.id.x) * 256.0);
}

TEST_CASE("a nonatomic model stays and its parts descend on their own")
{
    seedRealCatalog();
    Sandbox sandbox;

    const std::string cluster = R"({"class":"Model","name":"Rocks","children":[)" + partNode("Big", 10.0, 0.0, 10.0) +
                                "," + partNode("Small", 300.0, 0.0, 10.0) + "]}";
    const Partition partitioned = partition(sandbox, sceneText(cluster));

    CHECK(partitioned.result.report.records == 2);
    // Two cells, because `Nonatomic` files each part by its own position.
    CHECK(partitioned.cells.size() == 2);
    // The model itself is still there, so a path to it resolves and a script
    // that looks for it finds it -- empty, until the grid brings its parts.
    CHECK(partitioned.result.scene.find("\"Rocks\"") != std::string::npos);
    for (const auto& cell : partitioned.cells) {
        CHECK(cell.second.groups.empty());
    }
}

TEST_CASE("an atomic model that cannot be a group is not quietly demoted")
{
    seedRealCatalog();
    Sandbox sandbox;

    // A light under it: a cell record cannot say what it is, so half the model
    // would arrive. `Atomic` exists to prevent exactly that, so the whole thing
    // stays authored and the report says so.
    const std::string machine = R"({"class":"Model","name":"Machine","properties":{"StreamingMode":"Atomic"},)"
                                R"("children":[)" +
                                partNode("Body", 10.0, 0.0, 10.0) + R"(,{"class":"PointLight","name":"Bulb"}]})";
    const Partition partitioned = partition(sandbox, sceneText(machine));

    CHECK(partitioned.result.report.records == 0);
    CHECK(partitioned.result.report.unstreamable >= 1);
    CHECK(partitioned.result.scene.find("\"Body\"") != std::string::npos);
}

TEST_CASE("a cell a built world already owns is left alone")
{
    seedRealCatalog();
    Sandbox sandbox;

    PartitionSettings settings;
    settings.cellTaken = [](asset::ChunkId id) { return id.x == 0 && id.z == 0; };

    const Partition partitioned = partition(
        sandbox, sceneText(partNode("Home", 8.0, 0.0, 8.0) + "," + partNode("Away", 300.0, 0.0, 8.0)), settings);

    CHECK(partitioned.result.report.occupied == 1);
    CHECK(partitioned.result.report.records == 1);
    CHECK(partitioned.result.scene.find("\"Home\"") != std::string::npos);
    CHECK(partitioned.result.scene.find("\"Away\"") == std::string::npos);
}

TEST_CASE("the world a partition produces is the world the scene would have")
{
    seedRealCatalog();

    const std::string children =
        partNode("Ground", 0.0, -0.5, 0.0, 40.0) + "," +
        partNodeWith("Prop", 12.0, 1.0, 300.0, 3.0, R"("Friction":0.02,"Density":4,"CanCollide":false,)",
                     R"(,"tags":["Spin"])") +
        "," + R"({"class":"Model","name":"Gate","properties":{"StreamingMode":"Atomic"},"children":[)" +
        partNode("Pier", 250.0, 5.0, 40.0, 6.0) + "," + partNode("Beam", 262.0, 5.0, 40.0, 6.0) + "]}," +
        R"({"class":"Camera","name":"MainCamera"})";
    const std::string text = sceneText(children, R"({"CurrentCamera":"Workspace.MainCamera"})");

    // The world as the scene alone describes it.
    Sandbox whole;
    REQUIRE(!scene::readScene(whole.world, text).has_value());
    const std::vector<PartRow> expected = partsOf(whole.world, whole.workspace);
    REQUIRE(expected.size() == 4);

    // And the world a partition produces: the residual, plus every cell.
    Sandbox streamed;
    const Partition partitioned = partition(streamed, text);
    REQUIRE(!scene::readScene(streamed.world, partitioned.result.scene).has_value());

    // Through the ENCODED bytes rather than the cell in hand: that is the path
    // a run takes, and a field the writer forgets is a field the world loses
    // without anything saying so.
    scene::StreamingGlue glue(streamed.world, streamed.workspace);
    for (const auto& cell : partitioned.cells) {
        asset::Chunk decoded;
        REQUIRE(!asset::decodeChunk(asset::encodeChunk(cell.second), decoded).has_value());
        (void)glue.materialize(decoded.id, decoded);
    }

    const std::vector<PartRow> actual = partsOf(streamed.world, streamed.workspace);
    REQUIRE(actual.size() == expected.size());
    for (core::usize i = 0; i < expected.size(); ++i) {
        CHECK(actual[i].name == expected[i].name);
        CHECK(actual[i].position.x == doctest::Approx(expected[i].position.x));
        CHECK(actual[i].position.y == doctest::Approx(expected[i].position.y));
        CHECK(actual[i].position.z == doctest::Approx(expected[i].position.z));
        // The f32 halves are compared exactly: a cell carries them as the bits
        // it was given, so a tolerance would pass a record that lost one.
        CHECK(actual[i].size.x == expected[i].size.x);
        CHECK(actual[i].color.r == expected[i].color.r);
        CHECK(actual[i].anchored == expected[i].anchored);
        CHECK(actual[i].canCollide == expected[i].canCollide);
        CHECK(actual[i].friction == expected[i].friction);
        CHECK(actual[i].density == expected[i].density);
        CHECK(actual[i].collisionGroup == expected[i].collisionGroup);
        // The tag is what a script in a streamed world finds things by, so a
        // partition that lost one would be a world nothing can address.
        CHECK(actual[i].tags == expected[i].tags);
    }

    // And the atomic model came back as a model with its parts under it.
    bool foundGate = false;
    std::vector<core::InstanceId> descendants;
    streamed.world.collectDescendants(streamed.workspace, descendants);
    for (const core::InstanceId id : descendants) {
        if (streamed.world.atoms().text(streamed.world.name(id)) == "Gate") {
            foundGate = true;
            CHECK(streamed.world.childCount(id) == 2);
        }
    }
    CHECK(foundGate);
}

TEST_CASE("the partitioner never holds the world")
{
    seedRealCatalog();

    // The measurement the gate asks for, and it is a measurement rather than an
    // inspection: partition two worlds an order of magnitude apart and require
    // the peak to be the SAME, not merely small. A peak that grows with N is a
    // partitioner that is holding the world however small the constant looks.
    const auto peakFor = [](core::u32 count) {
        std::string children;
        children.reserve(static_cast<core::usize>(count) * 140);
        for (core::u32 i = 0; i < count; ++i) {
            if (i != 0)
                children.push_back(',');
            const double x = static_cast<double>(i % 4096) * 3.0;
            const double z = static_cast<double>(i / 4096) * 3.0;
            children += partNode("P", x, 0.0, z);
        }
        Sandbox sandbox;
        const Partition partitioned = partition(sandbox, sceneText(children));
        CHECK(partitioned.result.report.records == count);
        return partitioned.result.report.peakScratchInstances;
    };

    const core::u32 small = peakFor(2000);
    const core::u32 large = peakFor(40000);
    CHECK(small == large);
    // One authored node at a time, and a node is one instance. The bound is
    // named rather than merely equal, so a change that started holding two
    // fails here instead of passing on a tie.
    CHECK(large <= 4);
}

TEST_CASE("a part below a folder leaves the scene it was taken out of (D084)")
{
    seedRealCatalog();
    Sandbox sandbox;

    // **The whole tree, not just its top row.** A world authored with its
    // scenery in a folder -- which is what an Explorer full of loose parts drives
    // anybody to do -- put every one of those parts in a cell AND left every one
    // of them in the scene, so a partitioned world loaded twice. The copies are
    // coincident, so the picture is right and only the counters disagree.
    const std::string nested = R"({"class":"Folder","name":"Scenery","children":[)" +
                               partNode("Tile", 10.0, 0.0, 10.0) + "," + partNode("Far", 400.0, 0.0, 10.0) + "]}";
    const Partition partitioned = partition(sandbox, sceneText(nested));

    CHECK(partitioned.result.report.records == 2);
    CHECK(partitioned.result.scene.find("\"Tile\"") == std::string::npos);
    CHECK(partitioned.result.scene.find("\"Far\"") == std::string::npos);
    // The folder itself stays: a path to it still resolves, and it is where the
    // grid parents nothing -- streaming has a folder of its own.
    CHECK(partitioned.result.scene.find("\"Scenery\"") != std::string::npos);
}

TEST_CASE("a part three folders down leaves too")
{
    seedRealCatalog();
    Sandbox sandbox;

    const std::string deep = R"({"class":"Folder","name":"A","children":[)"
                             R"({"class":"Folder","name":"B","children":[)"
                             R"({"class":"Folder","name":"C","children":[)" +
                             partNode("Deep", 12.0, 0.0, 12.0) + "]}]}]}";
    const Partition partitioned = partition(sandbox, sceneText(deep));

    CHECK(partitioned.result.report.records == 1);
    CHECK(partitioned.result.scene.find("\"Deep\"") == std::string::npos);
    CHECK(partitioned.result.scene.find("\"C\"") != std::string::npos);
}

TEST_CASE("partitioning a partitioned scene moves nothing")
{
    seedRealCatalog();

    // The property D084 wants asserted, and the cheapest statement of "the
    // scene it hands the boot holds only what stayed": run it again and there
    // is nothing left to take.
    const std::string nested = R"({"class":"Folder","name":"Scenery","children":[)" +
                               partNode("Tile", 10.0, 0.0, 10.0) + "]}," +
                               R"({"class":"Model","name":"Beacon","properties":{"StreamingMode":"Persistent"},)"
                               R"("children":[)" +
                               partNode("Mast", 4000.0, 0.0, 4000.0) + "]}";

    Sandbox first;
    const Partition once = partition(first, sceneText(nested));
    REQUIRE(once.result.report.records == 1);

    Sandbox again;
    const Partition twice = partition(again, once.result.scene);
    CHECK(twice.result.report.records == 0);
    CHECK(twice.cells.empty());
    // And it is a FIXED POINT, byte for byte: a second pass that reformatted
    // what it did not move would make the cache key change on every run.
    CHECK(twice.result.scene == once.result.scene);
}

namespace {

// A `Terrain` node carrying `field`, the way the scene writer puts one down.
[[nodiscard]] std::string terrainNode(const asset::TerrainField& field)
{
    asset::TerrainCell whole;
    whole.settings = field.settings();
    whole.field = field;
    const std::vector<std::byte> bytes = asset::encodeTerrainCell(whole);
    const std::string blob =
        core::base64Encode(std::span<const core::u8>(reinterpret_cast<const core::u8*>(bytes.data()), bytes.size()));
    return R"({"class":"Terrain","name":"Terrain","properties":{"Position":[100,0,0]},"terrain":")" + blob + "\"}";
}

// The `voxels` member for `grid`, on `writeVoxels`' terms.
[[nodiscard]] std::string voxelsMember(const asset::VoxelGrid& grid)
{
    std::string text = R"({"blockSize":1,"types":[{"name":"Stone"}],"chunks":[)";
    bool first = true;
    for (const asset::VoxelChunkKey key : grid.chunkKeys()) {
        const std::vector<core::u8> runs = asset::encodeVoxelChunk(*grid.findChunk(key));
        text += (first ? "" : ",") + std::string(R"({"x":)") + std::to_string(key.x) + R"(,"y":)" +
                std::to_string(key.y) + R"(,"z":)" + std::to_string(key.z) + R"(,"blocks":")" +
                core::base64Encode(runs) + "\"}";
        first = false;
    }
    return text + "]}";
}

struct FieldPartition
{
    PartitionResult result;
    std::map<asset::ChunkId, std::vector<std::byte>> cells;
};

[[nodiscard]] FieldPartition partitionFields(Sandbox& sandbox, const std::string& text)
{
    FieldPartition out;
    PartitionSettings settings;
    settings.fieldSink = [&out](asset::ChunkId id, std::span<const std::byte> bytes) {
        out.cells.emplace(id, std::vector<std::byte>(bytes.begin(), bytes.end()));
        return scene::PartitionCellWritten{"field_" + std::to_string(id.x) + "_" + std::to_string(id.z) + "_" +
                                               std::to_string(id.layer),
                                           static_cast<core::u32>(bytes.size())};
    };
    const std::optional<core::EngineError> error =
        scene::partitionScene(sandbox.world, text, settings, {}, {}, out.result);
    REQUIRE(!error.has_value());
    return out;
}

} // namespace

TEST_CASE("a large terrain and block world leave the scene as cells, and keep their settings")
{
    seedRealCatalog();
    Sandbox sandbox;

    asset::TerrainField field(asset::FieldSettings{.voxelSize = 1.0f, .minHeight = -40.0f, .maxHeight = 60.0f});
    (void)asset::fillBlock(field, core::DVec3{0.0, -30.0, 0.0}, core::Vec3{300.0f, 60.0f, 300.0f}, 1);
    asset::VoxelGrid grid;
    (void)grid.fill(-100, 0, -100, 100, 3, 100, 1);

    std::string text = sceneText(terrainNode(field));
    text.pop_back();
    text += R"(,"voxels":)" + voxelsMember(grid) + "}";

    const FieldPartition partitioned = partitionFields(sandbox, text);
    const scene::PartitionReport& report = partitioned.result.report;
    CHECK(report.terrainCells >= 16);
    CHECK(report.voxelCells >= 16);
    CHECK(partitioned.result.fieldIndex.chunks.size() == report.terrainCells + report.voxelCells);
    CHECK(std::is_sorted(partitioned.result.fieldIndex.chunks.begin(), partitioned.result.fieldIndex.chunks.end(),
                         [](const asset::ChunkIndexEntry& a, const asset::ChunkIndexEntry& b) { return a.id < b.id; }));

    // **Every cell reads back, and together they are the field.**
    asset::TerrainField ground(field.settings());
    asset::VoxelGrid blocks;
    for (const auto& entry : partitioned.cells) {
        if (entry.first.layer == asset::FieldLayerTerrain) {
            asset::TerrainCell cell;
            REQUIRE_FALSE(asset::decodeTerrainCell(entry.second, cell).has_value());
            ground.shareFrom(cell.field);
        }
        else {
            asset::VoxelCell cell;
            REQUIRE_FALSE(asset::decodeVoxelCell(entry.second, cell).has_value());
            blocks.shareFrom(cell.grid);
        }
    }
    CHECK(ground.digest() == field.digest());
    CHECK(blocks.digest() == grid.digest());

    // **The terrain is scored where it sits**: the node's Position moves every
    // cell's bounds a hundred metres east.
    for (const asset::ChunkIndexEntry& entry : partitioned.result.fieldIndex.chunks) {
        if (entry.id.layer != asset::FieldLayerTerrain)
            continue;
        const double side = 64.0;
        CHECK(entry.bounds.min.x == doctest::Approx(100.0 + entry.id.x * side));
        CHECK(entry.bounds.min.y == doctest::Approx(-40.0));
        CHECK(entry.bounds.max.y == doctest::Approx(60.0));
    }

    // **And the scene boots with the settings and without the ground.**
    const std::string& residual = partitioned.result.scene;
    CHECK(residual.find(R"("chunks":[])") != std::string::npos);
    CHECK(residual.find(R"("types":[{"name":"Stone"}])") != std::string::npos);
    CHECK(residual.size() < text.size() / 4);
}

TEST_CASE("a field too small to stream stays in the scene, byte for byte")
{
    seedRealCatalog();
    Sandbox sandbox;

    asset::TerrainField field(asset::FieldSettings{.voxelSize = 1.0f, .minHeight = -40.0f, .maxHeight = 60.0f});
    (void)asset::fillBlock(field, core::DVec3{0.0, -30.0, 0.0}, core::Vec3{40.0f, 60.0f, 40.0f}, 1);
    const std::string text = sceneText(terrainNode(field));

    const FieldPartition partitioned = partitionFields(sandbox, text);
    CHECK(partitioned.result.report.terrainCells == 0);
    CHECK(partitioned.cells.empty());
    CHECK(partitioned.result.scene == text);
}
