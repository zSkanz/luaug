// **The wire and the world hash describe the same state** (the owner's
// mandate, S3; the unresolved list of `phase-2-4-plan.md`).
//
// Two hand-kept lists name the simulation state that is not a property: the
// wire schema's `Component` fields, which a replica is sent, and the block
// `world_hash.cpp` hashes by hand. Nothing compared them. A field added to the
// wire and not to the hash is state two runs can disagree on with the hash
// saying they agree -- the one thing the hash exists to catch.
//
// So this does not compare names, which would be a third list. It changes
// every such field, through the same `applyField` a replica uses, and requires
// the world hash to move. Here rather than in the replication module because
// only here is every module's class registered: `Decal` and `Lighting` are the
// renderer's, and their properties are what the hash walks.
#include "luaug/core/i18n.h"
#include "luaug/replication/extract.h"
#include "luaug/replication/field.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>

#include "../../audio/generated/class_descriptors.gen.h"
#include "../../input/generated/class_descriptors.gen.h"
#include "../../render/generated/class_descriptors.gen.h"
#include "../../replication/generated/wire_schema.gen.h"
#include "../../scene/generated/class_descriptors.gen.h"
#include "../../ui/generated/class_descriptors.gen.h"

using namespace luaug;
namespace wire = luaug::replication::generated;

namespace {

// Two values of an encoding, of which at least one differs from any value.
void candidates(core::AtomTable& atoms, wire::Encoding encoding, replication::FieldValue& a, replication::FieldValue& b)
{
    switch (encoding) {
    case wire::Encoding::Bool:
        replication::setBool(a, true);
        replication::setBool(b, false);
        return;
    case wire::Encoding::U8:
        replication::setU8(a, 1);
        replication::setU8(b, 2);
        return;
    case wire::Encoding::U16:
        replication::setU16(a, 1);
        replication::setU16(b, 2);
        return;
    case wire::Encoding::U32:
        replication::setU32(a, 1);
        replication::setU32(b, 2);
        return;
    case wire::Encoding::I32:
        replication::setI32(a, 1);
        replication::setI32(b, 2);
        return;
    case wire::Encoding::F32:
        replication::setF32(a, 0.375f);
        replication::setF32(b, 0.625f);
        return;
    case wire::Encoding::F64:
        replication::setF64(a, 0.375);
        replication::setF64(b, 0.625);
        return;
    case wire::Encoding::Position:
        replication::setPosition(a, core::DVec3{3.0, 4.0, 5.0});
        replication::setPosition(b, core::DVec3{6.0, 7.0, 8.0});
        return;
    case wire::Encoding::CFrameD: {
        core::CFrameD frame;
        frame.position = core::DVec3{3.0, 4.0, 5.0};
        replication::setCFrame(a, frame);
        frame.position = core::DVec3{6.0, 7.0, 8.0};
        replication::setCFrame(b, frame);
        return;
    }
    case wire::Encoding::NameAtom:
        replication::setU32(a, atoms.intern("wire-hash-a").id);
        replication::setU32(b, atoms.intern("wire-hash-b").id);
        return;
    case wire::Encoding::InstanceRef:
        // A reference names another instance, which this sweep does not make;
        // none of the state fields is one today, and `REQUIRE` below says so
        // if that changes.
        replication::setNetId(a, replication::NetId{});
        replication::setNetId(b, replication::NetId{});
        return;
    default:
        // Vector3, Color3 and the rest are three floats.
        replication::setVec3(a, core::Vec3{0.25f, 0.5f, 0.75f});
        replication::setVec3(b, core::Vec3{0.75f, 0.5f, 0.25f});
        return;
    }
}

} // namespace

TEST_CASE("every replicated field that is not a property is state the world hash covers")
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::generated::registerEnums(enums, atoms);
    scene::generated::registerClasses(classes, atoms);
    luaug::render::generated::registerClasses(classes, atoms);
    luaug::ui::generated::registerClasses(classes, atoms);
    luaug::audio::generated::registerClasses(classes, atoms);
    luaug::input::generated::registerClasses(classes, atoms);
    scene::World world(classes, enums, atoms, 7u);

    std::size_t checked = 0;
    for (const wire::ClassDesc& desc : wire::Classes) {
        const scene::ClassId declared = classes.findId(atoms.intern(desc.name));
        CAPTURE(std::string(desc.name));
        REQUIRE_MESSAGE(declared != scene::InvalidClass, "the wire names a class no module registers");
        // An abstract class is reached through the first concrete one that is
        // one, and whose schema is this one (a `Part` for `BasePart`).
        scene::ClassId classId = scene::InvalidClass;
        for (scene::ClassId at = 1; at < static_cast<scene::ClassId>(classes.classCount()); ++at) {
            const scene::ClassDescriptor* candidate = classes.find(at);
            if (candidate != nullptr && !scene::hasFlag(candidate->flags, scene::ClassFlags::Abstract) &&
                classes.isA(at, declared)) {
                const core::InstanceId probe = world.create(at);
                const bool same = probe.valid() && replication::schemaFor(world, probe) == &desc;
                if (probe.valid())
                    world.destroy(probe);
                if (same) {
                    classId = at;
                    break;
                }
            }
        }
        REQUIRE_MESSAGE(classId != scene::InvalidClass, "no class the engine can create carries this schema");
        const core::InstanceId id = world.create(classId);
        REQUIRE(id.valid());
        // A part needs its body for `Anchored` and `CanCollide`, which the
        // physics mirror adds to a real one.
        if (world.parts().find(id) != nullptr && world.rigidBodies().find(id) == nullptr)
            world.rigidBodies().add(id, scene::RigidBodyComponent{});

        replication::FieldSet fields;
        REQUIRE(replication::extractFields(world, id, desc, fields));
        for (core::usize index = 0; index < fields.size(); ++index) {
            const wire::FieldDesc* field = replication::fieldAt(desc, index);
            REQUIRE(field != nullptr);
            if (field->source != wire::Source::Component)
                continue;
            CAPTURE(std::string(field->name));
            replication::FieldValue a;
            replication::FieldValue b;
            REQUIRE(field->encoding != wire::Encoding::InstanceRef);
            candidates(atoms, field->encoding, a, b);
            const replication::FieldValue changed = a == fields[index] ? b : a;
            if (changed == fields[index])
                continue;

            const core::u64 before = world.worldHash();
            const core::u16 wireId = replication::wireIdAt(desc, index);
            REQUIRE(replication::applyField(world, id, desc, replication::FieldDelta{wireId, changed}));
            CHECK_MESSAGE(world.worldHash() != before,
                          "a replica is sent this, and the world hash does not see it change");
            ++checked;
        }
        // Not written back: `Emitted` is a running total a replica only moves
        // forward, so a fresh instance per class is the clean slate.
        world.destroy(id);
    }
    // Every class the wire carries with state of its own was reached.
    CHECK(checked > 30);
}
