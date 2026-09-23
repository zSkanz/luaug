#include "luaug/replication/extract.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cstring>

#include "wire_schema.gen.h"

namespace luaug::replication {
namespace {

using core::InstanceId;
using core::usize;
using generated::ClassDesc;
using generated::Encoding;
using generated::FieldDesc;
using generated::Source;

[[nodiscard]] const ClassDesc* schemaNamed(std::string_view name)
{
    for (const ClassDesc& desc : generated::Classes) {
        if (desc.name == name) {
            return &desc;
        }
    }
    return nullptr;
}

// Reads one component-sourced field.
//
// **A switch on the POOL and then on the FIELD NAME, and that is deliberate.**
// The generator emits a description and never a reader (ADR 0069, and the same
// rule `native_accessors.cpp` follows), so this is where the engine's own types
// are named. The alternative -- teaching the generator what a component is --
// would make every storage change a generator change.
[[nodiscard]] bool readComponent(const scene::World& world, InstanceId id, const FieldDesc& field, FieldValue& out)
{
    if (field.pool == "parts") {
        const scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr) {
            return false;
        }
        if (field.name == "CFrame") {
            setCFrame(out, part->cframe);
            return true;
        }
        if (field.name == "Size") {
            setVec3(out, part->size);
            return true;
        }
        if (field.name == "Color") {
            setVec3(out, core::Vec3{part->color.r, part->color.g, part->color.b});
            return true;
        }
        if (field.name == "Transparency") {
            setF32(out, part->transparency);
            return true;
        }
        return false;
    }

    // **A part's simulation state is a different component from its shape**, and
    // the schema said `parts` for one commit before the compiler said otherwise.
    // `scene` (L3) must not learn what a body is, so `anchored` and `canCollide`
    // live where the mirror reads them.
    if (field.pool == "rigidBodies") {
        const scene::RigidBodyComponent* body = world.rigidBodies().find(id);
        if (body == nullptr) {
            return false;
        }
        if (field.name == "Anchored") {
            setBool(out, body->anchored);
            return true;
        }
        if (field.name == "CanCollide") {
            setBool(out, body->canCollide);
            return true;
        }
        return false;
    }

    if (field.pool == "characterBodies") {
        const scene::CharacterBodyComponent* body = world.characterBodies().find(id);
        if (body == nullptr) {
            return false;
        }
        if (field.name == "VerticalVelocity") {
            setF32(out, body->verticalVelocity);
            return true;
        }
        if (field.name == "Grounded") {
            setBool(out, body->grounded);
            return true;
        }
        if (field.name == "State") {
            setI32(out, body->state);
            return true;
        }
        return false;
    }

    if (field.pool == "lighting") {
        const scene::LightingComponent* lighting = world.lighting().find(id);
        if (lighting == nullptr) {
            return false;
        }
        if (field.name == "ClockTime") {
            setF32(out, lighting->clockTime);
            return true;
        }
        if (field.name == "GeographicLatitude") {
            setF32(out, lighting->geographicLatitude);
            return true;
        }
        if (field.name == "Ambient") {
            setVec3(out, core::Vec3{lighting->ambient.r, lighting->ambient.g, lighting->ambient.b});
            return true;
        }
        if (field.name == "Brightness") {
            setF32(out, lighting->brightness);
            return true;
        }
        if (field.name == "FogColor") {
            setVec3(out, core::Vec3{lighting->fogColor.r, lighting->fogColor.g, lighting->fogColor.b});
            return true;
        }
        if (field.name == "FogStart") {
            setF32(out, lighting->fogStart);
            return true;
        }
        if (field.name == "FogEnd") {
            setF32(out, lighting->fogEnd);
            return true;
        }
        if (field.name == "ExposureCompensation") {
            setF32(out, lighting->exposureCompensation);
            return true;
        }
        return false;
    }

    if (field.pool == "decals") {
        const scene::DecalComponent* decal = world.decals().find(id);
        if (decal == nullptr) {
            return false;
        }
        if (field.name == "CFrame") {
            setCFrame(out, decal->cframe);
            return true;
        }
        if (field.name == "Size") {
            setVec3(out, decal->size);
            return true;
        }
        if (field.name == "Texture") {
            setU32(out, decal->texture.id);
            return true;
        }
        if (field.name == "Color") {
            setVec3(out, core::Vec3{decal->color.r, decal->color.g, decal->color.b});
            return true;
        }
        if (field.name == "Transparency") {
            setF32(out, decal->transparency);
            return true;
        }
        return false;
    }

    if (field.pool == "particleEmitters") {
        const scene::ParticleEmitterComponent* emitter = world.particleEmitters().find(id);
        if (emitter == nullptr) {
            return false;
        }
        if (field.name == "Enabled") {
            setBool(out, emitter->enabled);
            return true;
        }
        if (field.name == "Rate") {
            setF32(out, emitter->rate);
            return true;
        }
        if (field.name == "Lifetime") {
            setF32(out, emitter->lifetime);
            return true;
        }
        if (field.name == "Speed") {
            setF32(out, emitter->speed);
            return true;
        }
        if (field.name == "SpreadAngle") {
            setF32(out, emitter->spreadAngle);
            return true;
        }
        if (field.name == "Acceleration") {
            setVec3(out, emitter->acceleration);
            return true;
        }
        if (field.name == "Drag") {
            setF32(out, emitter->drag);
            return true;
        }
        if (field.name == "Color") {
            setVec3(out, core::Vec3{emitter->color.r, emitter->color.g, emitter->color.b});
            return true;
        }
        if (field.name == "ColorEnd") {
            setVec3(out, core::Vec3{emitter->colorEnd.r, emitter->colorEnd.g, emitter->colorEnd.b});
            return true;
        }
        if (field.name == "Size") {
            setF32(out, emitter->size);
            return true;
        }
        if (field.name == "SizeEnd") {
            setF32(out, emitter->sizeEnd);
            return true;
        }
        if (field.name == "Transparency") {
            setF32(out, emitter->transparency);
            return true;
        }
        if (field.name == "TransparencyEnd") {
            setF32(out, emitter->transparencyEnd);
            return true;
        }
        if (field.name == "LightEmission") {
            setF32(out, emitter->lightEmission);
            return true;
        }
        if (field.name == "Brightness") {
            setF32(out, emitter->brightness);
            return true;
        }
        if (field.name == "Shape") {
            setI32(out, emitter->shape);
            return true;
        }
        if (field.name == "Emitted") {
            setU32(out, static_cast<core::u32>(emitter->emitted));
            return true;
        }
        return false;
    }

    if (field.pool == "models") {
        const scene::ModelComponent* model = world.models().find(id);
        if (model == nullptr) {
            return false;
        }
        if (field.name == "Scale") {
            setF32(out, model->scale);
            return true;
        }
        return false;
    }

    return false;
}

[[nodiscard]] bool writeComponent(scene::World& world, InstanceId id, const FieldDesc& field, const FieldValue& value)
{
    if (field.pool == "parts") {
        scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr) {
            return false;
        }
        if (field.name == "CFrame") {
            part->cframe = asCFrame(value);
            return true;
        }
        if (field.name == "Size") {
            part->size = asVec3(value);
            return true;
        }
        if (field.name == "Color") {
            const core::Vec3 colour = asVec3(value);
            part->color = core::Color3{colour.x, colour.y, colour.z};
            return true;
        }
        if (field.name == "Transparency") {
            part->transparency = asF32(value);
            return true;
        }
        return false;
    }

    if (field.pool == "rigidBodies") {
        scene::RigidBodyComponent* body = world.rigidBodies().find(id);
        if (body == nullptr) {
            return false;
        }
        if (field.name == "Anchored") {
            body->anchored = asBool(value);
            return true;
        }
        if (field.name == "CanCollide") {
            body->canCollide = asBool(value);
            return true;
        }
        return false;
    }

    if (field.pool == "characterBodies") {
        scene::CharacterBodyComponent* body = world.characterBodies().find(id);
        if (body == nullptr) {
            return false;
        }
        if (field.name == "VerticalVelocity") {
            body->verticalVelocity = asF32(value);
            return true;
        }
        if (field.name == "Grounded") {
            body->grounded = asBool(value);
            return true;
        }
        if (field.name == "State") {
            body->state = static_cast<core::i32>(asU32(value));
            return true;
        }
        return false;
    }

    if (field.pool == "lighting") {
        scene::LightingComponent* lighting = world.lighting().find(id);
        if (lighting == nullptr) {
            return false;
        }
        const auto colour = [&value] {
            const core::Vec3 v = asVec3(value);
            return core::Color3{v.x, v.y, v.z};
        };
        if (field.name == "ClockTime")
            lighting->clockTime = asF32(value);
        else if (field.name == "GeographicLatitude")
            lighting->geographicLatitude = asF32(value);
        else if (field.name == "Ambient")
            lighting->ambient = colour();
        else if (field.name == "Brightness")
            lighting->brightness = asF32(value);
        else if (field.name == "FogColor")
            lighting->fogColor = colour();
        else if (field.name == "FogStart")
            lighting->fogStart = asF32(value);
        else if (field.name == "FogEnd")
            lighting->fogEnd = asF32(value);
        else if (field.name == "ExposureCompensation")
            lighting->exposureCompensation = asF32(value);
        else
            return false;
        return true;
    }

    if (field.pool == "decals") {
        scene::DecalComponent* decal = world.decals().find(id);
        if (decal == nullptr) {
            return false;
        }
        if (field.name == "CFrame") {
            decal->cframe = asCFrame(value);
            return true;
        }
        if (field.name == "Size") {
            decal->size = asVec3(value);
            return true;
        }
        if (field.name == "Texture") {
            decal->texture = core::NameAtom{asU32(value)};
            return true;
        }
        if (field.name == "Color") {
            const core::Vec3 colour = asVec3(value);
            decal->color = core::Color3{colour.x, colour.y, colour.z};
            return true;
        }
        if (field.name == "Transparency") {
            decal->transparency = asF32(value);
            return true;
        }
        return false;
    }

    if (field.pool == "particleEmitters") {
        scene::ParticleEmitterComponent* emitter = world.particleEmitters().find(id);
        if (emitter == nullptr) {
            return false;
        }
        if (field.name == "Enabled") {
            emitter->enabled = asBool(value);
            return true;
        }
        if (field.name == "Rate") {
            emitter->rate = asF32(value);
            return true;
        }
        if (field.name == "Lifetime") {
            emitter->lifetime = asF32(value);
            return true;
        }
        if (field.name == "Speed") {
            emitter->speed = asF32(value);
            return true;
        }
        if (field.name == "SpreadAngle") {
            emitter->spreadAngle = asF32(value);
            return true;
        }
        if (field.name == "Acceleration") {
            emitter->acceleration = asVec3(value);
            return true;
        }
        if (field.name == "Drag") {
            emitter->drag = asF32(value);
            return true;
        }
        if (field.name == "Color") {
            const core::Vec3 colour = asVec3(value);
            emitter->color = core::Color3{colour.x, colour.y, colour.z};
            return true;
        }
        if (field.name == "ColorEnd") {
            const core::Vec3 colour = asVec3(value);
            emitter->colorEnd = core::Color3{colour.x, colour.y, colour.z};
            return true;
        }
        if (field.name == "Size") {
            emitter->size = asF32(value);
            return true;
        }
        if (field.name == "SizeEnd") {
            emitter->sizeEnd = asF32(value);
            return true;
        }
        if (field.name == "Transparency") {
            emitter->transparency = asF32(value);
            return true;
        }
        if (field.name == "TransparencyEnd") {
            emitter->transparencyEnd = asF32(value);
            return true;
        }
        if (field.name == "LightEmission") {
            emitter->lightEmission = asF32(value);
            return true;
        }
        if (field.name == "Brightness") {
            emitter->brightness = asF32(value);
            return true;
        }
        if (field.name == "Shape") {
            emitter->shape = static_cast<core::i32>(asU32(value));
            return true;
        }
        if (field.name == "Emitted") {
            // The replica keeps its own running total and moves it by the
            // difference, so a wrap of the 32 bits on the wire costs nothing.
            const core::u32 sent = asU32(value);
            emitter->emitted += static_cast<core::u32>(sent - static_cast<core::u32>(emitter->emitted));
            return true;
        }
        return false;
    }

    if (field.pool == "models") {
        scene::ModelComponent* model = world.models().find(id);
        if (model == nullptr) {
            return false;
        }
        if (field.name == "Scale") {
            model->scale = asF32(value);
            return true;
        }
        return false;
    }

    return false;
}

} // namespace

const ClassDesc* schemaFor(const scene::World& world, InstanceId id)
{
    scene::ClassId classId = world.classOf(id);
    // **Walks up**, so `Part` and `MeshPart` find `BasePart`'s schema. Bounded
    // by the hierarchy's own depth, which the registry guarantees is acyclic.
    for (int guard = 0; guard < 64 && classId != scene::InvalidClass; ++guard) {
        const scene::ClassDescriptor* descriptor = world.classes().find(classId);
        if (descriptor == nullptr) {
            return nullptr;
        }
        if (const ClassDesc* desc = schemaNamed(world.atoms().text(descriptor->name)); desc != nullptr) {
            return desc;
        }
        classId = descriptor->super;
    }
    return nullptr;
}

namespace {

// The class whose fields this one also carries, or null.
[[nodiscard]] const ClassDesc* baseOf(const ClassDesc& desc) noexcept
{
    if (desc.base < 0 || static_cast<usize>(desc.base) >= std::size(generated::Classes))
        return nullptr;
    return &generated::Classes[desc.base];
}

} // namespace

usize fieldCount(const ClassDesc& desc)
{
    const ClassDesc* base = baseOf(desc);
    return std::size(generated::CommonFields) + (base != nullptr ? base->fields.size() : 0) + desc.fields.size();
}

core::u16 wireIdAt(const ClassDesc& desc, usize index)
{
    // **An id is only unique within its own half of the set**, and this is the
    // whole reason this function exists rather than `field->id` at every call
    // site. `api/wire/schema.luau` numbers the common fields from 1 and each
    // class's fields from 1 independently -- so `Name` (common id 1) and
    // `CFrame` (`BasePart` id 1) are the same number, and a decoder matching on
    // the raw id would write a name into a transform.
    //
    // The top bit says which half. Ids are capped at 32767 by that, which is
    // four orders of magnitude more than any class will have, and the wire form
    // is what both ends compare -- so it is the permanent id, not the index.
    //
    // A class that extends another carries a third half, its base's fields,
    // under the next bit down -- so `CharacterBody`'s `VerticalVelocity` (own
    // id 1) and its inherited `CFrame` (base id 1) cannot collide either.
    constexpr core::u16 ClassBit = 0x8000;
    constexpr core::u16 BaseBit = 0x4000;
    const usize common = std::size(generated::CommonFields);
    const ClassDesc* base = baseOf(desc);
    const usize inherited = base != nullptr ? base->fields.size() : 0;
    const FieldDesc* field = fieldAt(desc, index);
    if (field == nullptr) {
        return 0;
    }
    if (index < common)
        return field->id;
    if (index < common + inherited)
        return static_cast<core::u16>(field->id | BaseBit);
    return static_cast<core::u16>(field->id | ClassBit);
}

const FieldDesc* fieldAt(const ClassDesc& desc, usize index)
{
    const usize common = std::size(generated::CommonFields);
    if (index < common) {
        return &generated::CommonFields[index];
    }
    usize own = index - common;
    if (const ClassDesc* base = baseOf(desc); base != nullptr) {
        if (own < base->fields.size())
            return &base->fields[own];
        own -= base->fields.size();
    }
    if (own < desc.fields.size()) {
        return &desc.fields[own];
    }
    return nullptr;
}

bool extractFields(const scene::World& world, InstanceId id, const ClassDesc& desc, FieldSet& out)
{
    const usize count = fieldCount(desc);
    // **Into a scratch and only then into `out`.** A half-filled set diffed
    // against a baseline reports its unread half as changed, every tick, for
    // ever -- so a field that cannot be read is the whole extraction failing
    // rather than a zero nobody notices.
    FieldSet scratch(count);

    for (usize at = 0; at < count; ++at) {
        const FieldDesc* field = fieldAt(desc, at);
        if (field == nullptr) {
            return false;
        }

        if (field->source == Source::Component) {
            if (!readComponent(world, id, *field, scratch[at])) {
                return false;
            }
            continue;
        }

        // The common set: what every instance has whatever its class.
        if (field->name == "Name") {
            setU32(scratch[at], world.name(id).id);
            continue;
        }
        if (field->name == "Parent") {
            // **The parent's own id, resolved to a network id by the caller.**
            // This layer stores the instance id and knows nothing about peers;
            // mapping it is the sender's job, because which network id a peer
            // holds is a fact about that peer.
            setU32(scratch[at], world.parentOf(id).index);
            continue;
        }
        return false;
    }

    out = std::move(scratch);
    return true;
}

void diffFields(const ClassDesc& desc, std::span<const FieldValue> baseline, std::span<const FieldValue> current,
                std::vector<FieldDelta>& out)
{
    out.clear();

    if (baseline.size() != current.size()) {
        // Everything changed. See the header: this is a baseline taken under a
        // different class, and sending all of it is what makes the replica
        // correct rather than subtly wrong.
        for (usize at = 0; at < current.size(); ++at) {
            out.push_back(FieldDelta{wireIdAt(desc, at), current[at]});
        }
        return;
    }

    for (usize at = 0; at < current.size(); ++at) {
        if (baseline[at] == current[at]) {
            continue;
        }
        out.push_back(FieldDelta{wireIdAt(desc, at), current[at]});
    }
}

usize clearReplicated(scene::World& world, InstanceId root)
{
    std::vector<InstanceId> doomed;
    for (InstanceId child = world.firstChild(root); child.valid(); child = world.nextSibling(child)) {
        if (schemaFor(world, child) != nullptr)
            doomed.push_back(child);
    }
    for (const InstanceId id : doomed)
        (void)world.destroy(id);
    world.retireDestroyed();
    return doomed.size();
}

usize clearForReplica(scene::World& world, InstanceId workspace)
{
    usize cleared = clearReplicated(world, workspace);
    const InstanceId dataModel = world.parentOf(workspace);
    for (InstanceId service = dataModel.valid() ? world.firstChild(dataModel) : InstanceId{}; service.valid();
         service = world.nextSibling(service)) {
        const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(service));
        if (descriptor == nullptr)
            continue;
        const std::string_view name = world.atoms().text(descriptor->name);
        if (name == "ReplicatedStorage") {
            cleared += clearReplicated(world, service);
        }
        else if (name == "ServerStorage") {
            std::vector<InstanceId> doomed;
            for (InstanceId child = world.firstChild(service); child.valid(); child = world.nextSibling(child))
                doomed.push_back(child);
            for (const InstanceId id : doomed)
                (void)world.destroy(id);
            world.retireDestroyed();
            cleared += doomed.size();
        }
    }
    return cleared;
}

bool applyField(scene::World& world, InstanceId id, const ClassDesc& desc, const FieldDelta& delta)
{
    const usize count = fieldCount(desc);
    for (usize at = 0; at < count; ++at) {
        const FieldDesc* field = fieldAt(desc, at);
        if (field == nullptr || wireIdAt(desc, at) != delta.id) {
            continue;
        }

        if (field->source == Source::Component) {
            return writeComponent(world, id, *field, delta.value);
        }

        if (field->name == "Name") {
            world.setName(id, core::NameAtom{asU32(delta.value)});
            return true;
        }
        if (field->name == "Parent") {
            // Resolved by the caller, which is the only layer that knows which
            // network id means which local instance.
            return true;
        }
        return false;
    }
    return false;
}

} // namespace luaug::replication
