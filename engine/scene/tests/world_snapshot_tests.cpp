#include <doctest/doctest.h>

// doctest stringifies whatever a CHECK compares, and that needs the stream
// operators for std::string to be visible here.
#include <ostream>
#include <string>
#include <vector>

#include "scene_fixture.h"

using luaug::core::InstanceId;
using luaug::core::u16;
using luaug::core::u32;
using luaug::core::u64;
using luaug::core::Vec3;
using luaug::scene::AttributeMap;
using luaug::scene::TagSet;
using luaug::scene::Value;
using luaug::scene::World;
using luaug::scene::WorldSnapshot;
using luaug::scene::testing::Fixture;

namespace {

// A world holding one of everything `world_hash.cpp` walks: a tree three deep,
// two children sharing a name (the duplicate-name chain, ADR 0026), properties
// declared by two different classes, an Instance-valued property pointing
// across the tree, attributes and tags.
struct Scene
{
    InstanceId root;
    InstanceId alpha;
    InstanceId beta;
    InstanceId alphaTwin;
    InstanceId leaf;
    InstanceId rig;
};

[[nodiscard]] Scene build(Fixture& fixture)
{
    World& world = fixture.world;

    Scene scene;
    scene.root = fixture.folder("Root");
    scene.alpha = fixture.part("Alpha");
    scene.beta = fixture.part("Beta");
    scene.alphaTwin = fixture.part("Alpha");
    scene.leaf = fixture.part("Leaf");
    scene.rig = fixture.model("Rig");

    REQUIRE_FALSE(world.setParent(scene.alpha, scene.root).has_value());
    REQUIRE_FALSE(world.setParent(scene.beta, scene.root).has_value());
    REQUIRE_FALSE(world.setParent(scene.alphaTwin, scene.root).has_value());
    REQUIRE_FALSE(world.setParent(scene.rig, scene.root).has_value());
    REQUIRE_FALSE(world.setParent(scene.leaf, scene.beta).has_value());

    CHECK(world.setProperty(scene.alpha, fixture.schema.transparencyProperty, Value{0.25}) ==
          World::SetResult::Changed);
    CHECK(world.setProperty(scene.beta, fixture.schema.sizeProperty, Value{Vec3{2.0f, 3.0f, 4.0f}}) ==
          World::SetResult::Changed);
    CHECK(world.setProperty(scene.leaf, fixture.schema.shapeProperty, Value{2.0}) == World::SetResult::Changed);
    CHECK(world.setProperty(scene.rig, fixture.schema.primaryPartProperty, Value{scene.alpha}) ==
          World::SetResult::Changed);

    CHECK(world.setAttribute(scene.alpha, fixture.atom("Health"), Value{100.0}));
    CHECK(world.setAttribute(scene.alpha, fixture.atom("Faction"), Value{std::string("Red")}));
    CHECK(world.setAttribute(scene.leaf, fixture.atom("Fragile"), Value{true}));

    CHECK(world.addTag(scene.alpha, fixture.atom("Enemy")));
    CHECK(world.addTag(scene.beta, fixture.atom("Enemy")));
    CHECK(world.addTag(scene.leaf, fixture.atom("Pickup")));

    world.engineState().simTime = 12.5;
    world.engineState().tick = 750;
    world.engineState().masterVolume = 0.4f;

    // Drained so the queue starts empty, which is the frame boundary a snapshot
    // is taken at.
    (void)world.changes().take();
    return scene;
}

// Everything a play session does: builds, destroys, renames, reparents, writes
// properties, and moves the state no property exposes.
void mutate(Fixture& fixture, const Scene& scene)
{
    World& world = fixture.world;

    const InstanceId spawned = fixture.part("Spawned");
    REQUIRE_FALSE(world.setParent(spawned, scene.root).has_value());

    // A whole subtree gone, and retired, so the slots are actually recycled.
    REQUIRE(world.destroy(scene.beta));
    world.retireDestroyed();

    world.setName(scene.alpha, fixture.atom("Renamed"));

    // Reorders the children: re-parenting to a different parent and back
    // appends, which moves it to the end.
    REQUIRE_FALSE(world.setParent(scene.alphaTwin, InstanceId{}).has_value());
    REQUIRE_FALSE(world.setParent(scene.alphaTwin, scene.root).has_value());

    CHECK(world.setProperty(scene.alpha, fixture.schema.transparencyProperty, Value{0.9}) == World::SetResult::Changed);
    CHECK(world.setAttribute(scene.alpha, fixture.atom("Health"), Value{3.0}));
    CHECK(world.setAttribute(scene.alpha, fixture.atom("Faction"), Value{}));
    CHECK(world.addTag(scene.alpha, fixture.atom("Burning")));
    CHECK(world.removeTag(scene.alpha, fixture.atom("Enemy")));

    world.engineState().simTime = 99.0;
    world.engineState().tick = 5940;
    world.engineState().masterVolume = 1.0f;

    (void)world.changes().take();
}

[[nodiscard]] std::vector<std::string> tagNames(const Fixture& fixture, InstanceId id)
{
    TagSet tags;
    fixture.world.collectTags(id, tags);
    std::vector<std::string> out;
    for (const luaug::core::NameAtom tag : tags)
        out.emplace_back(fixture.schema.atoms.text(tag));
    return out;
}

[[nodiscard]] Value attributeOf(Fixture& fixture, InstanceId id, std::string_view attribute)
{
    return fixture.world.getAttribute(id, fixture.atom(attribute));
}

} // namespace

// --- The differential -------------------------------------------------------

TEST_CASE("a restore puts the world hash back, and the mutation moved it")
{
    Fixture fixture;
    const Scene scene = build(fixture);

    const u64 before = fixture.world.worldHash();
    const WorldSnapshot snapshot = fixture.world.snapshot();

    mutate(fixture, scene);

    // The inverse half, and it is the half that stops the round trip below
    // passing for the wrong reason: a hash blind to what `mutate` did would
    // report equality whether or not anything was restored (ADR 0017's
    // addendum, D043).
    CHECK(fixture.world.worldHash() != before);

    fixture.world.restore(snapshot);

    CHECK(fixture.world.worldHash() == before);
}

TEST_CASE("a snapshot is a copy, so it survives the world it came from moving on")
{
    Fixture fixture;
    const Scene scene = build(fixture);

    const u64 before = fixture.world.worldHash();
    const WorldSnapshot snapshot = fixture.world.snapshot();

    // Two rounds of play and stop off ONE snapshot: the first restore must not
    // consume it, or an editor could press play exactly once.
    for (int round = 0; round < 2; ++round) {
        mutate(fixture, scene);
        CHECK(fixture.world.worldHash() != before);
        fixture.world.restore(snapshot);
        CHECK(fixture.world.worldHash() == before);
    }
}

// --- Lifetime ---------------------------------------------------------------

TEST_CASE("instances created after the snapshot are gone after the restore")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    const InstanceId spawned = fixture.part("Spawned");
    REQUIRE_FALSE(fixture.world.setParent(spawned, scene.root).has_value());
    REQUIRE(fixture.world.alive(spawned));
    CHECK(fixture.world.childCount(scene.root) == 5);

    fixture.world.restore(snapshot);

    CHECK_FALSE(fixture.world.alive(spawned));
    CHECK(fixture.world.childCount(scene.root) == 4);
    CHECK_FALSE(fixture.world.findFirstChild(scene.root, fixture.atom("Spawned")).valid());
    // The component the spawned part carried goes with it.
    CHECK(fixture.world.parts().find(spawned) == nullptr);
}

TEST_CASE("instances destroyed after the snapshot are back after the restore")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    REQUIRE(fixture.world.destroy(scene.beta));
    fixture.world.retireDestroyed();
    REQUIRE_FALSE(fixture.world.alive(scene.beta));
    REQUIRE_FALSE(fixture.world.alive(scene.leaf));

    fixture.world.restore(snapshot);

    REQUIRE(fixture.world.alive(scene.beta));
    REQUIRE(fixture.world.alive(scene.leaf));
    CHECK_FALSE(fixture.world.destroyed(scene.beta));
    CHECK(fixture.nameOf(scene.beta) == "Beta");
    // The whole subtree, not just the root of it.
    CHECK(fixture.world.parentOf(scene.leaf) == scene.beta);
    CHECK(fixture.world.parentOf(scene.beta) == scene.root);
    // Its components came back with it, and so did what a `Destroying` clears.
    REQUIRE(fixture.world.parts().find(scene.beta) != nullptr);
    CHECK(fixture.world.parts().find(scene.beta)->size.y == doctest::Approx(3.0));
    CHECK(fixture.world.hasTag(scene.beta, fixture.atom("Enemy")));
    CHECK(fixture.world.getProperty(scene.leaf, fixture.schema.shapeProperty) == Value{2.0});
}

TEST_CASE("a handle held across a restore resolves to the same instance")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    mutate(fixture, scene);
    fixture.world.restore(snapshot);

    // The one an editor's selection is: taken before the snapshot, still good.
    REQUIRE(fixture.world.alive(scene.alpha));
    CHECK(fixture.nameOf(scene.alpha) == "Alpha");
    CHECK(fixture.world.parentOf(scene.alpha) == scene.root);
    CHECK(fixture.world.classOf(scene.alpha) == fixture.schema.partClass);
    CHECK(fixture.world.getProperty(scene.alpha, fixture.schema.transparencyProperty) == Value{0.25});
    // An Instance-valued property is a handle too, and it points where it did.
    CHECK(fixture.world.getProperty(scene.rig, fixture.schema.primaryPartProperty) == Value{scene.alpha});
}

// Rolling a generation back is what makes a pre-snapshot handle resolve again,
// and it is also the one way a handle could come to mean two things. The
// session recycles the same slot TWICE on purpose: with one cycle the restored
// generation and the next one handed out happen to miss each other by one, and
// a test that only did that would pass with the high-water mark removed. Both
// cases below were run against a build with it disabled, and both failed.
TEST_CASE("an id handed out during the session never comes back meaning something else")
{
    SUBCASE("from a slot that was free when the snapshot was taken")
    {
        Fixture fixture;

        const InstanceId scratch = fixture.folder("Scratch");
        REQUIRE(fixture.world.destroy(scratch));
        fixture.world.retireDestroyed();

        const WorldSnapshot snapshot = fixture.world.snapshot();

        std::vector<InstanceId> sessionIds;
        for (int cycle = 0; cycle < 2; ++cycle) {
            const InstanceId sessionOnly = fixture.folder("SessionOnly");
            REQUIRE(sessionOnly.valid());
            sessionIds.push_back(sessionOnly);
            REQUIRE(fixture.world.destroy(sessionOnly));
            fixture.world.retireDestroyed();
        }

        fixture.world.restore(snapshot);

        for (const InstanceId sessionOnly : sessionIds)
            CHECK_FALSE(fixture.world.alive(sessionOnly));

        // Nothing created afterwards may be handed one of those ids, however
        // many slots it takes to walk back over the free list.
        for (int i = 0; i < 8; ++i) {
            const InstanceId fresh = fixture.folder("Fresh");
            REQUIRE(fresh.valid());
            CHECK(fresh != scratch);
            for (const InstanceId sessionOnly : sessionIds)
                CHECK(fresh != sessionOnly);
        }
    }

    SUBCASE("from a slot that was occupied, and long after the restore")
    {
        Fixture fixture;

        // Occupied at snapshot time, so its generation comes back exactly and
        // the high-water mark has to outlive the restore to be any use: the
        // collision would only happen on the NEXT time the slot is recycled.
        const InstanceId held = fixture.folder("Held");
        const WorldSnapshot snapshot = fixture.world.snapshot();

        REQUIRE(fixture.world.destroy(held));
        fixture.world.retireDestroyed();

        std::vector<InstanceId> sessionIds;
        for (int cycle = 0; cycle < 2; ++cycle) {
            const InstanceId sessionOnly = fixture.folder("SessionOnly");
            REQUIRE(sessionOnly.valid());
            sessionIds.push_back(sessionOnly);
            REQUIRE(fixture.world.destroy(sessionOnly));
            fixture.world.retireDestroyed();
        }

        fixture.world.restore(snapshot);
        REQUIRE(fixture.world.alive(held));

        REQUIRE(fixture.world.destroy(held));
        fixture.world.retireDestroyed();

        for (int i = 0; i < 8; ++i) {
            const InstanceId fresh = fixture.folder("Fresh");
            REQUIRE(fresh.valid());
            CHECK(fresh != held);
            for (const InstanceId sessionOnly : sessionIds)
                CHECK(fresh != sessionOnly);
        }
    }
}

// --- The pieces, one at a time ----------------------------------------------

TEST_CASE("sibling order round-trips")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    const std::vector<std::string> before = fixture.childNames(scene.root);
    REQUIRE(before == std::vector<std::string>{"Alpha", "Beta", "Alpha", "Rig"});

    // Reparenting appends, so this moves Beta to the end.
    REQUIRE_FALSE(fixture.world.setParent(scene.beta, InstanceId{}).has_value());
    REQUIRE_FALSE(fixture.world.setParent(scene.beta, scene.root).has_value());
    REQUIRE(fixture.childNames(scene.root) == std::vector<std::string>{"Alpha", "Alpha", "Rig", "Beta"});

    fixture.world.restore(snapshot);

    CHECK(fixture.childNames(scene.root) == before);
    // The duplicate-name chain is rebuilt with the order, so the O(1) find
    // still answers with the FIRST of the two Alphas.
    CHECK(fixture.world.findFirstChild(scene.root, fixture.atom("Alpha")) == scene.alpha);
}

TEST_CASE("attributes round-trip, including one the session removed")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    CHECK(fixture.world.setAttribute(scene.alpha, fixture.atom("Health"), Value{3.0}));
    CHECK(fixture.world.setAttribute(scene.alpha, fixture.atom("Faction"), Value{}));
    CHECK(fixture.world.setAttribute(scene.alpha, fixture.atom("Poisoned"), Value{true}));

    fixture.world.restore(snapshot);

    CHECK(attributeOf(fixture, scene.alpha, "Health") == Value{100.0});
    CHECK(attributeOf(fixture, scene.alpha, "Faction") == Value{std::string("Red")});
    CHECK(attributeOf(fixture, scene.alpha, "Poisoned") == Value{});
    CHECK(attributeOf(fixture, scene.leaf, "Fragile") == Value{true});

    // Insertion order is what `GetAttributes` reports and what the hash walks,
    // so it is part of the state and not an implementation detail.
    AttributeMap attributes;
    fixture.world.collectAttributes(scene.alpha, attributes);
    REQUIRE(attributes.size() == 2);
    CHECK(fixture.schema.atoms.text(attributes[0].first) == "Health");
    CHECK(fixture.schema.atoms.text(attributes[1].first) == "Faction");
}

TEST_CASE("tags round-trip, and so does the reverse index")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    CHECK(fixture.world.addTag(scene.leaf, fixture.atom("Enemy")));
    CHECK(fixture.world.removeTag(scene.alpha, fixture.atom("Enemy")));
    CHECK(fixture.world.addTag(scene.rig, fixture.atom("Boss")));

    fixture.world.restore(snapshot);

    CHECK(tagNames(fixture, scene.alpha) == std::vector<std::string>{"Enemy"});
    CHECK(tagNames(fixture, scene.leaf) == std::vector<std::string>{"Pickup"});
    CHECK(tagNames(fixture, scene.rig).empty());

    // `GetTagged` reads a per-tag vector, and its order is observable.
    std::vector<InstanceId> tagged;
    fixture.world.collectTagged(fixture.atom("Enemy"), tagged);
    CHECK(tagged == std::vector<InstanceId>{scene.alpha, scene.beta});

    TagSet all;
    fixture.world.collectAllTags(all);
    CHECK(all.size() == 2);
}

TEST_CASE("the state the world hash does not reach round-trips too")
{
    Fixture fixture;
    (void)build(fixture);

    const u16 climbable = fixture.world.collisionGroups().add(fixture.atom("Climbable"));
    REQUIRE(climbable != luaug::scene::CollisionGroups::kInvalid);
    fixture.world.collisionGroups().setCollidable(climbable, luaug::scene::CollisionGroups::kDefault, false);
    const InstanceId focus = fixture.folder("Focus");
    fixture.world.streamingFoci().push_back(focus);
    fixture.world.engineState().streamingLoadRadius = 2048.0;

    const u32 nextDraw = fixture.world.rng().nextU32();
    const WorldSnapshot snapshot = fixture.world.snapshot();
    const u32 afterSnapshot = fixture.world.rng().nextU32();

    fixture.world.engineState().simTime = 99.0;
    fixture.world.engineState().tick = 5940;
    fixture.world.engineState().masterVolume = 1.0f;
    fixture.world.engineState().streamingLoadRadius = 64.0;
    fixture.world.streamingFoci().clear();
    (void)fixture.world.collisionGroups().add(fixture.atom("Water"));
    fixture.world.collisionGroups().setCollidable(climbable, luaug::scene::CollisionGroups::kDefault, true);
    for (int i = 0; i < 5; ++i)
        (void)fixture.world.rng().nextU32();

    fixture.world.restore(snapshot);

    CHECK(fixture.world.engineState().simTime == doctest::Approx(12.5));
    CHECK(fixture.world.engineState().tick == 750);
    CHECK(fixture.world.engineState().masterVolume == doctest::Approx(0.4));
    CHECK(fixture.world.engineState().streamingLoadRadius == doctest::Approx(2048.0));
    CHECK(fixture.world.streamingFoci() == std::vector<InstanceId>{focus});
    CHECK(fixture.world.collisionGroups().count() == 2);
    CHECK(fixture.world.collisionGroups().find(fixture.atom("Water")) == luaug::scene::CollisionGroups::kInvalid);
    CHECK_FALSE(fixture.world.collisionGroups().collidable(climbable, luaug::scene::CollisionGroups::kDefault));
    // The stream resumes where it was, which is the whole point of restoring a
    // position rather than a seed.
    CHECK(fixture.world.rng().nextU32() == afterSnapshot);
    CHECK(nextDraw != afterSnapshot);
}

TEST_CASE("a restore clears the change queue rather than replaying it")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const WorldSnapshot snapshot = fixture.world.snapshot();

    const InstanceId spawned = fixture.part("Spawned");
    REQUIRE_FALSE(fixture.world.setParent(spawned, scene.root).has_value());
    REQUIRE(fixture.world.destroy(scene.beta));
    REQUIRE_FALSE(fixture.world.changes().empty());

    fixture.world.restore(snapshot);

    // The entries named instances the restore has just replaced; the only
    // consumer is a VM the caller rebuilds.
    CHECK(fixture.world.changes().empty());
}

TEST_CASE("a restore leaves the world usable rather than merely correct")
{
    Fixture fixture;
    const Scene scene = build(fixture);
    const u64 before = fixture.world.worldHash();
    const WorldSnapshot snapshot = fixture.world.snapshot();

    mutate(fixture, scene);
    fixture.world.restore(snapshot);

    // Building the same thing again from the restored world reaches the same
    // hash as building it from the original one, which is the property a second
    // play session depends on.
    const InstanceId spawned = fixture.part("Spawned");
    REQUIRE_FALSE(fixture.world.setParent(spawned, scene.root).has_value());
    REQUIRE(fixture.world.destroy(spawned));
    fixture.world.retireDestroyed();
    CHECK(fixture.world.worldHash() == before);

    // And the tree it left is still internally consistent.
    REQUIRE(fixture.world.destroy(scene.alpha));
    fixture.world.retireDestroyed();
    CHECK(fixture.world.childCount(scene.root) == 3);
    CHECK(fixture.world.findFirstChild(scene.root, fixture.atom("Alpha")) == scene.alphaTwin);
}
