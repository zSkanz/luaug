// The partition cache (ADR 0053).
//
// **The cache is what makes "it runs on play" affordable.** Partitioning is
// arithmetic over every instance in a scene; doing it on every launch of a world
// somebody has not touched would be a tax on the loop the ADR exists to keep
// fast. So the question these ask is not "is it quick" -- a wall-clock assertion
// would be a test that fails on a busy machine -- but "did it do the work
// again", which the outcome says in a boolean.
#include "luaug/app/partition_cache.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <system_error>

#include "../../scene/generated/class_descriptors.gen.h"

using namespace luaug;

namespace {

void seedRealCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] std::string sceneOf(const std::string& children)
{
    return R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace","name":"Workspace","children":[)" +
           children + "]}}";
}

[[nodiscard]] std::string partNode(const std::string& name, double x)
{
    return R"({"class":"Part","name":")" + name + R"(","properties":{"CFrame":[)" + std::to_string(x) +
           R"(,0,0,1,0,0,0,1,0,0,0,1],"Size":[2,2,2],"Anchored":true}})";
}

// A project on disk, torn down with the test. Real files rather than a fake
// filesystem, because what is under test IS the filesystem behaviour: a hash in
// a directory name and a manifest that has to survive a process.
struct Project
{
    std::filesystem::path root;
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world;

    explicit Project(const std::string& name)
        : root(std::filesystem::temp_directory_path() / "luaug-partition-tests" / name),
          world(classes, enums, atoms, 3u)
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        REQUIRE(platform::createDirectories(root / "content" / "scenes"));
        scene::generated::registerEnums(enums, atoms);
        scene::generated::registerClasses(classes, atoms);
    }

    ~Project()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    [[nodiscard]] std::filesystem::path contentRoot() const { return root / "content"; }
    [[nodiscard]] std::filesystem::path scenePath() const { return contentRoot() / "scenes" / "main.scene.json"; }

    void write(const std::string& text) { REQUIRE(platform::writeTextFile(scenePath(), text)); }

    [[nodiscard]] app::PartitionOutcome run()
    {
        return app::partitionProject(world, root, contentRoot(), scenePath(), nullptr);
    }
};

} // namespace

TEST_CASE("a scene partitions once and then answers from the cache")
{
    seedRealCatalog();
    Project project("cache");
    // **Four parts, four cells**, because that is the floor a scene has to reach
    // before streaming it is worth anything (S8.2). Two would partition
    // perfectly well and then be refused, which is a different case and is the
    // one below this.
    project.write(sceneOf(partNode("A", 10.0) + "," + partNode("B", 400.0) + "," + partNode("C", 800.0) + "," +
                          partNode("D", 1200.0)));

    const app::PartitionOutcome first = project.run();
    CHECK(first.repartitioned);
    CHECK(first.active);
    CHECK(first.index.chunks.size() == 4);
    CHECK(first.report.records == 4);

    // The second press of play. Asserted on the boolean rather than on a clock:
    // a wall-clock comparison is a test that fails when the machine is busy.
    const app::PartitionOutcome second = project.run();
    CHECK_FALSE(second.repartitioned);
    CHECK(second.active);
    CHECK(second.index.chunks.size() == first.index.chunks.size());
    CHECK(second.directory == first.directory);
}

TEST_CASE("editing the scene repartitions, and the old cache does not linger")
{
    seedRealCatalog();
    Project project("edit");
    project.write(sceneOf(partNode("A", 10.0)));
    const app::PartitionOutcome first = project.run();
    REQUIRE(first.repartitioned);

    project.write(sceneOf(partNode("A", 10.0) + "," + partNode("C", 900.0)));
    const app::PartitionOutcome second = project.run();
    CHECK(second.repartitioned);
    CHECK(second.directory != first.directory);
    CHECK(second.index.chunks.size() == 2);

    // A directory per scene version would otherwise accumulate one per edit, in
    // the one place nobody looks.
    CHECK_FALSE(std::filesystem::exists(first.directory));
}

TEST_CASE("editing a stamp the scene names repartitions it too")
{
    seedRealCatalog();
    Project project("stamps");

    REQUIRE(platform::createDirectories(project.contentRoot() / "stamps"));
    const std::filesystem::path stamp = project.contentRoot() / "stamps" / "block.stamp.json";
    const auto writeStampFile = [&](double size) {
        const std::string text = R"({"format":"luaug-scene","version":1,"root":)" +
                                 std::string(R"({"class":"Part","name":"Block","properties":{)") +
                                 R"("CFrame":[40,0,0,1,0,0,0,1,0,0,0,1],"Size":[)" + std::to_string(size) + "," +
                                 std::to_string(size) + "," + std::to_string(size) + R"(],"Anchored":true}}})";
        REQUIRE(platform::writeTextFile(stamp, text));
    };

    writeStampFile(2.0);
    project.write(sceneOf(R"({"stamp":"stamps/block.stamp.json","name":"Block"})"));
    const app::PartitionOutcome first = project.run();
    CHECK(first.repartitioned);

    // Nothing in the scene's own bytes says a stamp moved, so a cache keyed on
    // the scene alone would hand back yesterday's buildings.
    const app::PartitionOutcome cached = project.run();
    CHECK_FALSE(cached.repartitioned);

    writeStampFile(9.0);
    const app::PartitionOutcome again = project.run();
    CHECK(again.repartitioned);
    // Same scene, so the same directory: the key is the scene and the stamps
    // are what VALIDATE it.
    CHECK(again.directory == first.directory);
}

TEST_CASE("a scene that streams nothing leaves the original scene to boot")
{
    seedRealCatalog();
    Project project("empty");
    project.write(sceneOf(R"({"class":"Camera","name":"MainCamera"})"));

    const app::PartitionOutcome outcome = project.run();
    CHECK_FALSE(outcome.active);
    // No residual to point at: the scene is unchanged, and a copy of it in the
    // cache would be a dependency a project with no cells has no use for.
    CHECK(outcome.scenePath.empty());
}

// --- The floor a scene has to reach (S8.2) -----------------------------------

TEST_CASE("a scene too small to be worth streaming is not streamed")
{
    // **Below the floor the partition costs and buys nothing.** Streaming exists
    // to keep a world out of memory until you are near it; a scene that fits in
    // two cells is entirely inside any sensible load radius, so nothing is ever
    // evicted and the whole thing is resident from the first pump either way.
    //
    // What it DOES cost is tree identity. A partitioned record carries no parent
    // path -- by design, because a path into `Workspace` is sometimes nil in a
    // world that is not all present (ADR 0053, rule 5) -- so a small project
    // that got partitioned found its authored `Model` empty and its parts
    // somewhere else. The only way out was knowing the words
    // `StreamingMode = "Persistent"`, which the starter template had to say
    // about fifteen parts in order to appear at all.
    seedRealCatalog();
    Project project("small");
    project.write(sceneOf(partNode("A", 10.0) + "," + partNode("B", 400.0)));

    const app::PartitionOutcome outcome = project.run();
    CHECK_FALSE(outcome.active);
    // **The ORIGINAL scene boots**, which is what `active` being false means:
    // a residual scene path here would point at a copy of the file for a project
    // that has no use for one.
    CHECK(outcome.scenePath.empty());
}

TEST_CASE("the floor is about cells and not about parts")
{
    // Forty parts in one room is a room. What decides whether streaming helps is
    // how far apart things are, not how many there are -- and a floor counting
    // parts would stream a dense room and not stream a sparse continent.
    seedRealCatalog();
    Project project("dense");
    std::string parts;
    for (int index = 0; index < 40; ++index) {
        if (index > 0)
            parts += ",";
        parts += partNode("P" + std::to_string(index), 1.0 + static_cast<double>(index));
    }
    project.write(sceneOf(parts));

    const app::PartitionOutcome outcome = project.run();
    CHECK(outcome.report.records == 40);
    CHECK_FALSE(outcome.active);
}
