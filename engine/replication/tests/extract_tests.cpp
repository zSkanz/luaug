// Reading a world's replicated state and diffing it (ADR 0069).
//
// **No transport, no peers, no socket.** Everything here is a function of a
// world and a baseline, which is what lets the half that has to be right before
// anything is sent be tested in a process with no network in it.
#include "luaug/replication/extract.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <ostream>

#include "../../scene/tests/scene_fixture.h"
#include "wire_schema.gen.h"

using namespace luaug;
using namespace luaug::replication;

namespace {

struct Rig
{
    scene::testing::Fixture fixture;

    [[nodiscard]] scene::World& world() { return fixture.world; }

    [[nodiscard]] core::InstanceId part(core::DVec3 at)
    {
        const core::InstanceId id = fixture.world.create(fixture.schema.partClass);
        REQUIRE(id.valid());
        scene::PartComponent* component = fixture.world.parts().find(id);
        REQUIRE(component != nullptr);
        component->cframe.position = at;
        return id;
    }
};

} // namespace

TEST_CASE("a Part finds BasePart's schema by walking up")
{
    // **`Part` has no schema of its own and must not need one.** Requiring a row
    // per leaf would be requiring the wire schema to repeat the class hierarchy,
    // which is the second list the whole arrangement exists to prevent -- and it
    // is the same walk `wirecheck` does to decide a class is decided.
    Rig rig;
    const core::InstanceId id = rig.part({1.0, 2.0, 3.0});

    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);
    CHECK(desc->name == "BasePart");
}

TEST_CASE("an instance with no schema replicates nothing")
{
    Rig rig;
    const core::InstanceId folder = rig.fixture.world.create(rig.fixture.schema.folderClass);
    REQUIRE(folder.valid());

    // `Folder` IS replicated -- with no fields of its own, which is the point:
    // a `Parent` reference to one has to resolve on the replica.
    const generated::ClassDesc* desc = schemaFor(rig.world(), folder);
    REQUIRE(desc != nullptr);
    CHECK(desc->name == "Folder");
    CHECK(desc->fields.empty());
    // But it still carries the common set.
    CHECK(fieldCount(*desc) == 2);
}

TEST_CASE("extracting a part reads every field it declares")
{
    Rig rig;
    const core::InstanceId id = rig.part({10.0, -4.0, 2.5});
    // `Anchored` and `CanCollide` live in `rigidBodies`, so a part without one
    // is not fully extractable -- which the case below this one is about.
    rig.world().rigidBodies().add(id, scene::RigidBodyComponent{});
    scene::PartComponent* part = rig.world().parts().find(id);
    REQUIRE(part != nullptr);
    part->size = {2.0f, 3.0f, 4.0f};
    part->transparency = 0.25f;

    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);

    FieldSet fields;
    REQUIRE(extractFields(rig.world(), id, *desc, fields));
    CHECK(fields.size() == fieldCount(*desc));

    // The values are where the schema's order says they are.
    const core::usize common = std::size(generated::CommonFields);
    CHECK(asCFrame(fields[common + 0]).position.x == doctest::Approx(10.0));
    CHECK(static_cast<double>(asVec3(fields[common + 1]).y) == doctest::Approx(3.0));
    CHECK(static_cast<double>(asF32(fields[common + 3])) == doctest::Approx(0.25));
}

TEST_CASE("an extraction that cannot read a field fails whole")
{
    // **Never partially fills**, and this is the reason: a half-read set diffed
    // against a baseline reports its unread half as changed, every tick, for
    // ever -- and nothing reports a fault while it does.
    //
    // A `Part` in this fixture has a `PartComponent` and no `RigidBodyComponent`
    // unless one is added, and `Anchored` lives in the latter. So the extraction
    // has to refuse rather than write a zero nobody notices.
    Rig rig;
    const core::InstanceId id = rig.part({0.0, 0.0, 0.0});
    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);

    FieldSet fields;
    const bool complete = rig.world().rigidBodies().find(id) != nullptr;
    CHECK(extractFields(rig.world(), id, *desc, fields) == complete);
    if (!complete) {
        // And it left `out` alone rather than half-writing it.
        CHECK(fields.empty());
    }
}

TEST_CASE("a diff reports what changed and nothing else")
{
    Rig rig;
    const core::InstanceId id = rig.part({0.0, 0.0, 0.0});
    scene::RigidBodyComponent body;
    rig.world().rigidBodies().add(id, body);

    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);

    FieldSet baseline;
    REQUIRE(extractFields(rig.world(), id, *desc, baseline));

    std::vector<FieldDelta> deltas;
    diffFields(*desc, baseline, baseline, deltas);
    CHECK(deltas.empty());

    // Move it, and exactly one field is different.
    scene::PartComponent* part = rig.world().parts().find(id);
    REQUIRE(part != nullptr);
    part->cframe.position = {5.0, 0.0, 0.0};

    FieldSet current;
    REQUIRE(extractFields(rig.world(), id, *desc, current));
    diffFields(*desc, baseline, current, deltas);
    REQUIRE(deltas.size() == 1);
    CHECK(asCFrame(deltas[0].value).position.x == doctest::Approx(5.0));
}

TEST_CASE("a wire id is unique within a class, and the raw ids are not")
{
    // **The defect this pins.** `api/wire/schema.luau` numbers the common fields
    // from 1 and each class's fields from 1 independently, so `Name` (common id
    // 1) and `CFrame` (`BasePart` id 1) are the same number. A decoder matching
    // on the raw id would write a name into a transform.
    const generated::ClassDesc* basePart = nullptr;
    for (const generated::ClassDesc& desc : generated::Classes) {
        if (desc.name == "BasePart") {
            basePart = &desc;
        }
    }
    REQUIRE(basePart != nullptr);

    const generated::FieldDesc* name = fieldAt(*basePart, 0);
    const generated::FieldDesc* cframe = fieldAt(*basePart, std::size(generated::CommonFields));
    REQUIRE(name != nullptr);
    REQUIRE(cframe != nullptr);
    CHECK(name->name == "Name");
    CHECK(cframe->name == "CFrame");
    // The raw ids collide, which is legal and is why the wire ids do not.
    CHECK(name->id == cframe->id);
    CHECK(wireIdAt(*basePart, 0) != wireIdAt(*basePart, std::size(generated::CommonFields)));

    // Every wire id in the class is distinct.
    std::vector<core::u16> seen;
    for (core::usize at = 0; at < fieldCount(*basePart); ++at) {
        const core::u16 id = wireIdAt(*basePart, at);
        for (const core::u16 other : seen) {
            CAPTURE(at);
            CHECK(id != other);
        }
        seen.push_back(id);
    }
}

TEST_CASE("applying a delta writes the field its wire id names")
{
    Rig rig;
    const core::InstanceId id = rig.part({0.0, 0.0, 0.0});
    scene::RigidBodyComponent body;
    rig.world().rigidBodies().add(id, body);

    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);

    FieldValue moved;
    core::CFrameD target;
    target.position = {7.0, 8.0, 9.0};
    setCFrame(moved, target);

    const core::usize common = std::size(generated::CommonFields);
    FieldDelta delta{wireIdAt(*desc, common), moved};
    REQUIRE(applyField(rig.world(), id, *desc, delta));

    const scene::PartComponent* part = rig.world().parts().find(id);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.y == doctest::Approx(8.0));
    // And the name was not touched, which is what the wire id's top bit buys.
    CHECK(rig.world().name(id).valid());
}

TEST_CASE("a delta naming a field this class does not have is refused")
{
    // What a peer speaking a newer protocol looks like. A refusal rather than a
    // guess: a decoder that fell through to the nearest field would corrupt a
    // world rather than report a mismatch.
    Rig rig;
    const core::InstanceId id = rig.part({0.0, 0.0, 0.0});
    const generated::ClassDesc* desc = schemaFor(rig.world(), id);
    REQUIRE(desc != nullptr);

    FieldValue value;
    setU32(value, 42);
    CHECK_FALSE(applyField(rig.world(), id, *desc, FieldDelta{9999, value}));
}
