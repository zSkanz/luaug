// The scene format, which is what ADR 0047 decided a project's authored world
// lives in.
//
// **The world hash is NOT the oracle here, and that is worth saying because it
// is the oracle for the snapshot beside it.** The hash includes every
// `InstanceId`, and ids are minted at runtime — so a world that went through a
// file and came back is a different world by that measure however perfectly it
// round-tripped. The oracle for a FILE is the file: write, read into a fresh
// world, write again, and require the two texts to be identical. A serializer
// that loses something produces a shorter second text; one that invents
// something produces a longer one.
#include "luaug/scene/components.h"
#include "luaug/scene/scene_file.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <string_view>

#include "scene_fixture.h"

using namespace luaug;
using luaug::scene::SceneIoReport;
using luaug::scene::testing::Fixture;

namespace {
// The fixture has no Workspace class, and a scene is rooted at one. Attaching
// the component rather than teaching the fixture a class keeps this file from
// editing a header three other suites share.
core::InstanceId makeWorkspace(Fixture& fixture)
{
    const core::InstanceId id = fixture.world.create(fixture.schema.folderClass);
    fixture.world.setName(id, fixture.atom("Workspace"));
    fixture.world.workspaces().add(id, scene::WorkspaceComponent{});
    return id;
}

core::InstanceId partUnder(Fixture& fixture, core::InstanceId parent, std::string_view name, core::DVec3 position)
{
    const core::InstanceId id = fixture.world.create(fixture.schema.partClass);
    fixture.world.setName(id, fixture.atom(name));
    (void)fixture.world.setParent(id, parent);

    scene::PartComponent part;
    part.cframe.position = position;
    part.size = {2.0f, 4.0f, 6.0f};
    part.color = {0.25f, 0.5f, 0.75f};
    fixture.world.parts().add(id, part);
    return id;
}
} // namespace

TEST_CASE("a scene written, read and written again is the same bytes")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId tower = partUnder(fixture, workspace, "Tower", {10.0, 0.0, -5.0});
    (void)partUnder(fixture, tower, "Door", {0.0, 1.0, 0.0});
    (void)partUnder(fixture, workspace, "Ground", {0.0, -1.0, 0.0});

    SceneIoReport wrote;
    const std::string first = scene::writeScene(fixture.world, &wrote);
    CHECK(wrote.instances >= 4);

    Fixture reloaded;
    (void)makeWorkspace(reloaded);
    const std::optional<core::EngineError> error = scene::readScene(reloaded.world, first);
    REQUIRE_FALSE(error.has_value());

    const std::string second = scene::writeScene(reloaded.world);
    CHECK(first == second);
}

TEST_CASE("hierarchy and sibling order survive the round trip")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    (void)partUnder(fixture, workspace, "First", {});
    (void)partUnder(fixture, workspace, "Second", {});
    (void)partUnder(fixture, workspace, "Third", {});

    const std::string text = scene::writeScene(fixture.world);

    Fixture reloaded;
    const core::InstanceId target = makeWorkspace(reloaded);
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());

    std::vector<std::string> names;
    for (core::InstanceId child = reloaded.world.firstChild(target); child.valid();
         child = reloaded.world.nextSibling(child))
        names.emplace_back(reloaded.world.atoms().text(reloaded.world.name(child)));

    REQUIRE(names.size() == 3);
    CHECK(names[0] == "First");
    CHECK(names[1] == "Second");
    CHECK(names[2] == "Third");
}

TEST_CASE("properties survive, and a f64 position is not narrowed on the way")
{
    // Far enough out that an f32 round trip would visibly lose it, which is the
    // whole reason `CFrameD` exists (ADR 0014).
    const core::DVec3 far{4321.5, 12.25, -8765.75};

    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    (void)partUnder(fixture, workspace, "Distant", far);

    const std::string text = scene::writeScene(fixture.world);

    Fixture reloaded;
    const core::InstanceId target = makeWorkspace(reloaded);
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());

    const core::InstanceId child = reloaded.world.firstChild(target);
    REQUIRE(child.valid());
    const scene::PartComponent* part = reloaded.world.parts().find(child);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.x == doctest::Approx(far.x));
    CHECK(part->cframe.position.z == doctest::Approx(far.z));
    CHECK(part->size.y == doctest::Approx(4.0));
}

TEST_CASE("attributes and tags survive")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId part = partUnder(fixture, workspace, "Marked", {});
    (void)fixture.world.setAttribute(part, fixture.atom("Owner"), scene::Value{std::string("level-design")});
    (void)fixture.world.setAttribute(part, fixture.atom("Weight"), scene::Value{core::f64{2.5}});
    (void)fixture.world.addTag(part, fixture.atom("Breakable"));

    const std::string text = scene::writeScene(fixture.world);

    Fixture reloaded;
    const core::InstanceId target = makeWorkspace(reloaded);
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());

    const core::InstanceId child = reloaded.world.firstChild(target);
    REQUIRE(child.valid());
    CHECK(std::get<std::string>(reloaded.world.getAttribute(child, reloaded.atom("Owner"))) == "level-design");
    CHECK(std::get<core::f64>(reloaded.world.getAttribute(child, reloaded.atom("Weight"))) == doctest::Approx(2.5));
    CHECK(reloaded.world.hasTag(child, reloaded.atom("Breakable")));
}

TEST_CASE("a reference inside the scene resolves, including a forward one")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);

    // The model is written FIRST and points at a part written after it, which
    // is the case a single-pass loader gets wrong.
    const core::InstanceId model = fixture.world.create(fixture.schema.modelClass);
    fixture.world.setName(model, fixture.atom("Tower"));
    (void)fixture.world.setParent(model, workspace);
    const core::InstanceId base = partUnder(fixture, model, "Base", {});
    (void)fixture.world.setProperty(model, fixture.schema.primaryPartProperty, scene::Value{base});

    const std::string text = scene::writeScene(fixture.world);

    Fixture reloaded;
    const core::InstanceId target = makeWorkspace(reloaded);
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());

    const core::InstanceId loadedModel = reloaded.world.firstChild(target);
    REQUIRE(loadedModel.valid());
    const std::optional<scene::Value> primary =
        reloaded.world.getProperty(loadedModel, reloaded.schema.primaryPartProperty);
    REQUIRE(primary.has_value());
    const core::InstanceId referenced = std::get<core::InstanceId>(*primary);
    REQUIRE(referenced.valid());
    CHECK(reloaded.world.atoms().text(reloaded.world.name(referenced)) == "Base");
}

TEST_CASE("a reference to something outside the scene is dropped and counted, not guessed")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId model = fixture.world.create(fixture.schema.modelClass);
    fixture.world.setName(model, fixture.atom("Tower"));
    (void)fixture.world.setParent(model, workspace);

    // Outside `Workspace` entirely, so no path from the scene's root names it.
    const core::InstanceId orphan = fixture.world.create(fixture.schema.partClass);
    fixture.world.setName(orphan, fixture.atom("Elsewhere"));
    (void)fixture.world.setProperty(model, fixture.schema.primaryPartProperty, scene::Value{orphan});

    SceneIoReport report;
    const std::string text = scene::writeScene(fixture.world, &report);

    CHECK(report.droppedReferences >= 1);
    CHECK(text.find("Elsewhere") == std::string::npos);
}

TEST_CASE("a class this build does not have takes its subtree with it, and says so")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);

    const std::string text =
        R"({"format":"luaug-scene","version":1,"root":{"class":"Folder","name":"Workspace","children":[)"
        R"({"class":"HoverBike","name":"Bike","children":[{"class":"Part","name":"Wheel"}]},)"
        R"({"class":"Part","name":"Kept"}]}})";

    SceneIoReport report;
    REQUIRE_FALSE(scene::readScene(fixture.world, text, &report).has_value());

    CHECK(report.unknownClasses == 1);

    // The wheel is gone with the bike. A `Part` standing in for a class nobody
    // recognises would be a lie shaped like a recovery.
    std::vector<std::string> names;
    for (core::InstanceId child = fixture.world.firstChild(workspace); child.valid();
         child = fixture.world.nextSibling(child))
        names.emplace_back(fixture.world.atoms().text(fixture.world.name(child)));
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "Kept");
}

TEST_CASE("loading replaces rather than merges, so loading twice is loading once")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    (void)partUnder(fixture, workspace, "Only", {});

    const std::string text = scene::writeScene(fixture.world);

    REQUIRE_FALSE(scene::readScene(fixture.world, text).has_value());
    REQUIRE_FALSE(scene::readScene(fixture.world, text).has_value());

    CHECK(fixture.world.childCount(workspace) == 1);
}

TEST_CASE("a malformed file is refused rather than half applied")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    (void)partUnder(fixture, workspace, "Existing", {});

    CHECK(scene::readScene(fixture.world, "{not json").has_value());
    CHECK(scene::readScene(fixture.world, R"({"format":"something-else","version":1})").has_value());
    CHECK(scene::readScene(fixture.world, R"({"format":"luaug-scene","version":99})").has_value());

    // Untouched: a refusal that had already destroyed the world would be worse
    // than a load that failed.
    CHECK(fixture.world.childCount(workspace) == 1);
}

TEST_CASE("the scene's root applies to the Workspace that already exists")
{
    // `Workspace.CurrentCamera` is the property this case is really about: a
    // scene that restored every part and not the camera would load into a world
    // nothing can see. The fixture has no Workspace class, so the same
    // asymmetry is checked through a property the fixture does have.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId part = partUnder(fixture, workspace, "Anything", {});
    (void)fixture.world.setAttribute(workspace, fixture.atom("SceneName"), scene::Value{std::string("home")});
    (void)fixture.world.addTag(workspace, fixture.atom("Authored"));

    const std::string text = scene::writeScene(fixture.world);
    CHECK(part.valid());

    Fixture reloaded;
    const core::InstanceId target = makeWorkspace(reloaded);
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());

    CHECK(std::get<std::string>(reloaded.world.getAttribute(target, reloaded.atom("SceneName"))) == "home");
    CHECK(reloaded.world.hasTag(target, reloaded.atom("Authored")));
}

TEST_CASE("what a system made is not in the scene, and neither is what is under it")
{
    // The case that forced this: a save taken while the flagship was streaming
    // wrote 40 chunk folders and 1.4 MB of terrain into a file meant to describe
    // a project. A streamed part is as real to a tick as an authored one -- the
    // world hash counts it and must -- but nobody wrote it down, so a scene does
    // not record it.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    (void)partUnder(fixture, workspace, "Authored", {});

    const core::InstanceId chunk = fixture.world.create(fixture.schema.folderClass);
    fixture.world.setName(chunk, fixture.atom("Chunk_0_0_0"));
    (void)fixture.world.setParent(chunk, workspace);
    fixture.world.setGenerated(chunk, true);
    (void)partUnder(fixture, chunk, "Terrain", {});

    SceneIoReport report;
    const std::string text = scene::writeScene(fixture.world, &report);

    CHECK(text.find("Authored") != std::string::npos);
    CHECK(text.find("Chunk_0_0_0") == std::string::npos);
    // The whole subtree, not just the folder. Marking every part inside a chunk
    // would be the same statement a thousand times.
    CHECK(text.find("Terrain") == std::string::npos);
}

TEST_CASE("a reference to something a system made is dropped rather than dangling")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);

    const core::InstanceId chunk = fixture.world.create(fixture.schema.folderClass);
    fixture.world.setName(chunk, fixture.atom("Chunk_0_0_0"));
    (void)fixture.world.setParent(chunk, workspace);
    fixture.world.setGenerated(chunk, true);
    const core::InstanceId terrain = partUnder(fixture, chunk, "Terrain", {});

    const core::InstanceId model = fixture.world.create(fixture.schema.modelClass);
    fixture.world.setName(model, fixture.atom("Marker"));
    (void)fixture.world.setParent(model, workspace);
    (void)fixture.world.setProperty(model, fixture.schema.primaryPartProperty, scene::Value{terrain});

    SceneIoReport report;
    const std::string text = scene::writeScene(fixture.world, &report);

    // A path collected for something the file will not contain is a reference
    // that resolves to nothing on load, which is worse than a counted null.
    CHECK(report.droppedReferences >= 1);
    CHECK(text.find("Terrain") == std::string::npos);
}

// --- Stamps (ADR 0049) -------------------------------------------------------
//
// A stamp file is a scene of one subtree, and these are the four claims that
// makes: a stamp round-trips like a scene does; a scene holds a MARK rather than
// a copy; the mark is what makes changing the stamp change every instance of it;
// and a scene that names a stamp nobody can supply still opens.

TEST_CASE("a stamp is a scene of one subtree, and it round-trips")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{1.0, 2.0, 3.0});
    (void)partUnder(fixture, post, "Lantern", core::DVec3{1.0, 5.0, 3.0});

    SceneIoReport wrote;
    const std::string text = scene::writeStamp(fixture.world, post, &wrote);
    CHECK(wrote.instances == 2);

    Fixture other;
    const core::InstanceId otherWorkspace = makeWorkspace(other);
    SceneIoReport read;
    const core::InstanceId placed = scene::readStamp(other.world, text, otherWorkspace, "lantern-post", &read);
    REQUIRE(placed.valid());

    CHECK(other.world.parentOf(placed) == otherWorkspace);
    CHECK(other.world.atoms().text(other.world.name(placed)) == "Post");
    CHECK(other.world.childCount(placed) == 1);
    // **The mark is on the root and nowhere else.** Its children are the
    // stamp's contents, not instances of it.
    CHECK(other.world.atoms().text(other.world.stampOf(placed)) == "lantern-post");
    CHECK_FALSE(other.world.stampOf(other.world.firstChild(placed)).valid());
    // And "am I inside one" answers for the whole subtree, which is the
    // question the break rule actually asks.
    CHECK(other.world.stampRootOf(other.world.firstChild(placed)) == placed);
    CHECK_FALSE(other.world.stampRootOf(otherWorkspace).valid());

    // The file is the oracle, as it is for a scene: write it again out of the
    // world it came back into and require the same bytes.
    SceneIoReport again;
    CHECK(scene::writeStamp(other.world, placed, &again) == text);
}

TEST_CASE("a scene holds a stamped instance as a mark, a name and where it is")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{1.0, 2.0, 3.0});
    (void)partUnder(fixture, post, "Lantern", core::DVec3{1.0, 5.0, 3.0});

    const std::string stampText = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));

    // **A save needs the stamp to write a mark instead of a copy** (ADR 0051):
    // "what differs from the source" is a question about two trees, so the
    // source has to be built. Without a library every stamped instance is
    // written IN FULL and counted, which loses nothing and is what a caller
    // with no content root can honestly do.
    const auto source = [&stampText](std::string_view wanted) -> std::optional<std::string> {
        return wanted == "lantern-post" ? std::optional<std::string>(stampText) : std::nullopt;
    };
    scene::StampLibrary library(fixture.world, source);

    SceneIoReport wrote;
    const std::string sceneText = scene::writeScene(fixture.world, &wrote, &library);
    // The workspace and the post. **Not the lantern**: it belongs to the stamp
    // file, and writing it again would be the full copy that makes the whole
    // idea worthless.
    CHECK(wrote.instances == 2);
    CHECK(sceneText.find("Lantern") == std::string::npos);
    CHECK(sceneText.find("lantern-post") != std::string::npos);

    Fixture other;
    (void)makeWorkspace(other);
    SceneIoReport read;
    // The same source both ways: a save reads a stamp to diff against it and a
    // load reads it to build from it, and a project where those two disagreed
    // would be one where saving changed what loading produced.
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, &read, source).has_value());
    CHECK(read.stamped == 1);
    CHECK(read.missingStamps == 0);

    // Found by walking rather than by remembering an id, because a load builds
    // a new world and the ids are not the old ones.
    core::InstanceId placed;
    other.world.parts().forEach([&](core::InstanceId id, const scene::PartComponent&) {
        if (other.world.atoms().text(other.world.name(id)) == "Post")
            placed = id;
    });
    REQUIRE(placed.valid());
    // The lantern came back, and it came from the STAMP rather than from the
    // scene -- which is the whole mechanism.
    CHECK(other.world.childCount(placed) == 1);
    CHECK(other.world.atoms().text(other.world.stampOf(placed)) == "lantern-post");
}

TEST_CASE("changing a stamp changes every unbroken instance of it")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{});
    (void)partUnder(fixture, post, "Lantern", core::DVec3{});

    const std::string oneChild = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));
    const auto source = [&oneChild](std::string_view) -> std::optional<std::string> { return oneChild; };
    scene::StampLibrary library(fixture.world, source);
    const std::string sceneText = scene::writeScene(fixture.world, nullptr, &library);

    // The same stamp, with a second child. Nothing about the SCENE changed.
    (void)partUnder(fixture, post, "Bulb", core::DVec3{});
    const std::string twoChildren = scene::writeStamp(fixture.world, post);
    REQUIRE(oneChild != twoChildren);

    Fixture other;
    (void)makeWorkspace(other);
    const auto twoSource = [&twoChildren](std::string_view) -> std::optional<std::string> { return twoChildren; };
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, nullptr, twoSource).has_value());

    core::InstanceId placed;
    other.world.parts().forEach([&](core::InstanceId id, const scene::PartComponent&) {
        if (other.world.atoms().text(other.world.name(id)) == "Post")
            placed = id;
    });
    REQUIRE(placed.valid());
    // Two, from a scene file that never mentioned either of them.
    CHECK(other.world.childCount(placed) == 2);
}

TEST_CASE("a scene naming a stamp nobody can supply still opens, and says how many")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{});
    (void)partUnder(fixture, workspace, "Ground", core::DVec3{});
    const std::string stampText = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));
    const auto source = [&stampText](std::string_view) -> std::optional<std::string> { return stampText; };
    scene::StampLibrary library(fixture.world, source);
    const std::string sceneText = scene::writeScene(fixture.world, nullptr, &library);

    Fixture other;
    (void)makeWorkspace(other);
    SceneIoReport read;
    // No source at all, which is what an older build or a deleted file looks
    // like. Counted rather than fatal, exactly as an unknown class is.
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, &read).has_value());
    CHECK(read.missingStamps == 1);
    CHECK(read.stamped == 0);

    // And the rest of the scene arrived: one missing stamp is not a broken file.
    core::u32 found = 0;
    other.world.parts().forEach([&](core::InstanceId, const scene::PartComponent&) { ++found; });
    CHECK(found == 1);
}

TEST_CASE("an instance keeps its stamp through an edit, and the edit is an override")
{
    // **The model the human asked for, in one test.** An instance of a stamp
    // INHERITS from it: change the file and every instance changes with it,
    // except where an instance has said otherwise. What it has said otherwise
    // is the only thing about it the scene records.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{});
    const core::InstanceId lantern = partUnder(fixture, post, "Lantern", core::DVec3{});

    const std::string stampText = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));

    // One parameter of this one, changed. Not the stamp's -- this instance's.
    // `Transparency` because it is one the fixture's `BasePart` declares: an
    // override is only an override if the DESCRIPTOR knows the property, which
    // is the same rule the rest of the serializer follows.
    scene::PartComponent* body = fixture.world.parts().find(lantern);
    REQUIRE(body != nullptr);
    body->transparency = 0.5f;

    const auto source = [&stampText](std::string_view) -> std::optional<std::string> { return stampText; };
    scene::StampLibrary library(fixture.world, source);

    SceneIoReport wrote;
    const std::string sceneText = scene::writeScene(fixture.world, &wrote, &library);
    // **Still a mark**, and one override rather than a copy of the subtree.
    CHECK(wrote.stamped == 1);
    CHECK(wrote.unlinkedStamps == 0);
    CHECK(wrote.overrides >= 1);
    CHECK(sceneText.find("overrides") != std::string::npos);
    CHECK(sceneText.find("Lantern") != std::string::npos); // as a PATH, not as an instance

    Fixture other;
    (void)makeWorkspace(other);
    SceneIoReport read;
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, &read, source).has_value());
    CHECK(read.stamped == 1);
    CHECK(read.overrides >= 1);

    core::InstanceId placed;
    other.world.parts().forEach([&](core::InstanceId id, const scene::PartComponent&) {
        if (other.world.atoms().text(other.world.name(id)) == "Post")
            placed = id;
    });
    REQUIRE(placed.valid());
    // The link survived the edit, which is the reversal: it used to break.
    CHECK(other.world.atoms().text(other.world.stampOf(placed)) == "lantern-post");
    // And the override came back, on the child it was made on.
    const core::InstanceId placedLantern = other.world.firstChild(placed);
    REQUIRE(placedLantern.valid());
    const scene::PartComponent* restored = other.world.parts().find(placedLantern);
    REQUIRE(restored != nullptr);
    CHECK(static_cast<double>(restored->transparency) == doctest::Approx(0.5));
}

TEST_CASE("changing the stamp changes every instance, except where one has its own")
{
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{});
    const core::InstanceId lantern = partUnder(fixture, post, "Lantern", core::DVec3{});

    const std::string stampText = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));

    scene::PartComponent* body = fixture.world.parts().find(lantern);
    REQUIRE(body != nullptr);
    body->transparency = 0.5f;

    const auto original = [&stampText](std::string_view) -> std::optional<std::string> { return stampText; };
    scene::StampLibrary library(fixture.world, original);
    const std::string sceneText = scene::writeScene(fixture.world, nullptr, &library);

    // **The source moves on**: the lantern in the FILE grows and turns blue.
    Fixture edited;
    (void)makeWorkspace(edited);
    SceneIoReport ignored;
    const core::InstanceId reopened = scene::readStamp(edited.world, stampText, core::InstanceId{}, "x", &ignored);
    REQUIRE(reopened.valid());
    scene::PartComponent* sourceLantern = edited.world.parts().find(edited.world.firstChild(reopened));
    REQUIRE(sourceLantern != nullptr);
    sourceLantern->size = {9.0f, 9.0f, 9.0f};
    sourceLantern->transparency = 0.0f;
    const std::string changedStamp = scene::writeStamp(edited.world, reopened);

    Fixture other;
    (void)makeWorkspace(other);
    const auto changed = [&changedStamp](std::string_view) -> std::optional<std::string> { return changedStamp; };
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, nullptr, changed).has_value());

    core::InstanceId placed;
    other.world.parts().forEach([&](core::InstanceId id, const scene::PartComponent&) {
        if (other.world.atoms().text(other.world.name(id)) == "Post")
            placed = id;
    });
    REQUIRE(placed.valid());
    const scene::PartComponent* restored = other.world.parts().find(other.world.firstChild(placed));
    REQUIRE(restored != nullptr);
    // The size came from the FILE, because nobody overrode it here.
    CHECK(static_cast<double>(restored->size.x) == doctest::Approx(9.0));
    // The transparency did NOT: this instance said otherwise, and an override
    // is what "sem influenciar o anterior" means from the other side.
    CHECK(static_cast<double>(restored->transparency) == doctest::Approx(0.5));
}

TEST_CASE("a structural change is not an override, so the instance is written in full")
{
    // Adding a child to one instance is not "a parameter of this one" -- it is
    // a different thing. A format that recorded it would be inventing an
    // added-and-removed-object machinery nobody designed, so the instance is
    // written whole and its mark dropped: nothing is lost and the count says so.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId post = partUnder(fixture, workspace, "Post", core::DVec3{});
    (void)partUnder(fixture, post, "Lantern", core::DVec3{});

    const std::string stampText = scene::writeStamp(fixture.world, post);
    fixture.world.setStamp(post, fixture.atom("lantern-post"));
    (void)partUnder(fixture, post, "Banner", core::DVec3{});

    const auto source = [&stampText](std::string_view) -> std::optional<std::string> { return stampText; };
    scene::StampLibrary library(fixture.world, source);

    SceneIoReport wrote;
    const std::string sceneText = scene::writeScene(fixture.world, &wrote, &library);
    CHECK(wrote.stamped == 0);
    CHECK(wrote.unlinkedStamps == 1);
    // Written whole: both children are in the file by name.
    CHECK(sceneText.find("Lantern") != std::string::npos);
    CHECK(sceneText.find("Banner") != std::string::npos);

    Fixture other;
    (void)makeWorkspace(other);
    REQUIRE_FALSE(scene::readScene(other.world, sceneText, nullptr, source).has_value());
    core::InstanceId placed;
    other.world.parts().forEach([&](core::InstanceId id, const scene::PartComponent&) {
        if (other.world.atoms().text(other.world.name(id)) == "Post")
            placed = id;
    });
    REQUIRE(placed.valid());
    CHECK(other.world.childCount(placed) == 2);
    CHECK_FALSE(other.world.stampOf(placed).valid());
}

TEST_CASE("restamp moves every live instance of a stamp, in place, keeping its overrides")
{
    // **The reported defect**: "criei uma stamp linkada na workspace, editei a
    // stamp do content e não atualizou a stamp da workspace". Changing a stamp
    // changed every instance of it across a save and a LOAD -- the test above
    // says so -- and nothing at all while the world sat there. This is the same
    // arithmetic done live.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId original = partUnder(fixture, workspace, "Post", core::DVec3{});
    (void)partUnder(fixture, original, "Lantern", core::DVec3{});
    const std::string before = scene::writeStamp(fixture.world, original);
    (void)fixture.world.destroy(original);
    fixture.world.retireDestroyed();

    // Two linked instances, and a neighbour after them: the neighbour is what
    // proves the refresh happens IN PLACE rather than by rebuilding, because a
    // rebuilt instance would land after it.
    SceneIoReport placing;
    const core::InstanceId first = scene::readStamp(fixture.world, before, workspace, "post", &placing);
    const core::InstanceId second = scene::readStamp(fixture.world, before, workspace, "post", &placing);
    const core::InstanceId neighbour = partUnder(fixture, workspace, "Neighbour", core::DVec3{});
    REQUIRE(first.valid());
    REQUIRE(second.valid());

    // What the second one has of its own: one value on the instance, one on
    // something inside it, and a name.
    fixture.world.setName(second, fixture.atom("Second"));
    scene::PartComponent* body = fixture.world.parts().find(second);
    REQUIRE(body != nullptr);
    body->cframe.position = core::DVec3{5.0, 0.0, 0.0};
    scene::PartComponent* inner = fixture.world.parts().find(fixture.world.firstChild(second));
    REQUIRE(inner != nullptr);
    inner->transparency = 0.5f;

    // **The file moves on structurally**: a child added and a child taken away,
    // which is exactly what was reported and what a load-time refresh would
    // have handled and a live one did not.
    Fixture edited;
    (void)makeWorkspace(edited);
    SceneIoReport ignored;
    const core::InstanceId editing = scene::readStamp(edited.world, before, core::InstanceId{}, "post", &ignored);
    REQUIRE(editing.valid());
    (void)edited.world.destroy(edited.world.firstChild(editing));
    edited.world.retireDestroyed();
    const core::InstanceId glow = partUnder(edited, editing, "Glow", core::DVec3{});
    scene::PartComponent* glowBody = edited.world.parts().find(glow);
    REQUIRE(glowBody != nullptr);
    glowBody->size = {9.0f, 9.0f, 9.0f};
    const std::string after = scene::writeStamp(edited.world, editing);

    SceneIoReport moved;
    CHECK(scene::restamp(fixture.world, workspace, "post", before, after, &moved) == 2);

    for (const core::InstanceId instance : {first, second}) {
        CHECK(fixture.childNames(instance) == std::vector<std::string>{"Glow"});
        // Same instance, same parent, same place among its siblings.
        CHECK(fixture.world.alive(instance));
        CHECK(fixture.world.parentOf(instance) == workspace);
        // From the FILE, because nobody overrode it here.
        const scene::PartComponent* refreshed = fixture.world.parts().find(fixture.world.firstChild(instance));
        REQUIRE(refreshed != nullptr);
        CHECK(static_cast<double>(refreshed->size.x) == doctest::Approx(9.0));
    }
    CHECK(fixture.world.nextSibling(first) == second);
    CHECK(fixture.world.nextSibling(second) == neighbour);

    // **What the second one had of its own is still its own.** A refresh that
    // ate this would be a reset, and a person who had moved one lamp post would
    // find it back at the origin every time somebody else saved the file.
    CHECK(fixture.nameOf(second) == "Second");
    body = fixture.world.parts().find(second);
    REQUIRE(body != nullptr);
    CHECK(body->cframe.position.x == doctest::Approx(5.0));
    // The override INSIDE it named the lantern, and the lantern is gone from the
    // file -- dropped and counted rather than fatal, exactly as a load does it.
    CHECK(moved.refusedProperties > 0);
    const scene::PartComponent* replaced = fixture.world.parts().find(fixture.world.firstChild(second));
    REQUIRE(replaced != nullptr);
    CHECK(static_cast<double>(replaced->transparency) == doctest::Approx(0.0));
}

TEST_CASE("restamp leaves an instance whose shape has moved on, and counts it")
{
    // The rule the writer already applies, from the other side: somebody who
    // added a child to ONE lamp post is not asking for it to be thrown away the
    // next time the file is saved. It is not an instance of that stamp any more.
    Fixture fixture;
    const core::InstanceId workspace = makeWorkspace(fixture);
    const core::InstanceId original = partUnder(fixture, workspace, "Post", core::DVec3{});
    (void)partUnder(fixture, original, "Lantern", core::DVec3{});
    const std::string before = scene::writeStamp(fixture.world, original);
    (void)fixture.world.destroy(original);
    fixture.world.retireDestroyed();

    SceneIoReport ignored;
    const core::InstanceId mine = scene::readStamp(fixture.world, before, workspace, "post", &ignored);
    REQUIRE(mine.valid());
    (void)partUnder(fixture, mine, "Banner", core::DVec3{});

    Fixture edited;
    (void)makeWorkspace(edited);
    const core::InstanceId editing = scene::readStamp(edited.world, before, core::InstanceId{}, "post", &ignored);
    REQUIRE(editing.valid());
    (void)partUnder(edited, editing, "Glow", core::DVec3{});
    const std::string after = scene::writeStamp(edited.world, editing);

    SceneIoReport moved;
    CHECK(scene::restamp(fixture.world, workspace, "post", before, after, &moved) == 0);
    CHECK(moved.unlinkedStamps == 1);
    CHECK(fixture.childNames(mine) == std::vector<std::string>{"Lantern", "Banner"});

    // And a stamp nobody in the world is an instance of moves nothing.
    CHECK(scene::restamp(fixture.world, workspace, "some-other-stamp", before, after, &moved) == 0);
}
