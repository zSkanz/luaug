// The hand-written half of the reflection tables (architecture.md §4).
//
// `api/generator/gen_cpp.luau` emits the descriptors and declares these by
// name; this file is where a property actually meets its storage. Splitting it
// that way is what keeps the generator ignorant of components: moving a
// property's storage is an edit here, and the API definition does not notice.
//
// Every function is a plain function pointer's worth of work -- no captures, no
// allocation on the read path beyond what a `Value` costs -- because these are
// the innermost frames of the Instance facade, and architecture risk #1 is the
// facade's overhead eating the ECS's win.
#include <cmath>
#include <string>

#include "../generated/class_descriptors.gen.h"
#include "luaug/scene/world.h"

namespace luaug::scene::native
{
namespace
{

constexpr f64 RadiansToDegrees = 57.29577951308232;
constexpr f64 DegreesToRadians = 0.017453292519943295;

[[nodiscard]] const PartComponent* readPart(const World& world, core::InstanceId id) noexcept
{
    return world.parts().find(id);
}

[[nodiscard]] PartComponent* writePart(World& world, core::InstanceId id) noexcept
{
    return world.parts().find(id);
}

} // namespace

// --- Component hooks --------------------------------------------------------
//
// One pair per class that *declares* a component. `World::create` walks the
// ancestry root-first and calls each, so `Part` gets `PartComponent` from
// `BasePart`'s pair and declares none of its own -- declaring it twice would
// attach twice.

void attachPartComponents(World& world, core::InstanceId id)
{
    world.parts().add(id, PartComponent{});
}

void detachPartComponents(World& world, core::InstanceId id)
{
    world.parts().remove(id);
}

void attachModelComponents(World& world, core::InstanceId id)
{
    world.models().add(id, ModelComponent{});
}

void detachModelComponents(World& world, core::InstanceId id)
{
    world.models().remove(id);
}

void attachScriptComponents(World& world, core::InstanceId id)
{
    world.scripts().add(id, ScriptComponent{});
}

void detachScriptComponents(World& world, core::InstanceId id)
{
    world.scripts().remove(id);
}

// --- Instance ---------------------------------------------------------------

Value getInstanceName(const World& world, core::InstanceId id)
{
    return std::string(world.atoms().text(world.name(id)));
}

bool setInstanceName(World& world, core::InstanceId id, const Value& value)
{
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    world.setName(id, world.atoms().intern(*text));
    return true;
}

Value getInstanceParent(const World& world, core::InstanceId id)
{
    const core::InstanceId parent = world.parentOf(id);
    // An unparented instance reads as nil rather than as a dead handle: `nil`
    // is the honest answer and the one `Instance?` is typed for.
    return parent.valid() ? Value{parent} : Value{};
}

bool setInstanceParent(World& world, core::InstanceId id, const Value& value)
{
    core::InstanceId target;
    if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr)
        target = *reference;
    else if (valueType(value) != ValueType::Nil)
        return false;

    // Deliberately collapses three refusals into one `false`. A cycle and a
    // locked parent raise different keys, and the binding for `Parent` calls
    // `World::setParent` directly so it can tell them apart; this path exists
    // for the generic ones -- `Clone`, and whatever walks properties without
    // knowing what they mean.
    return !world.setParent(id, target).has_value();
}

Value getInstanceClassName(const World& world, core::InstanceId id)
{
    const ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    return descriptor == nullptr ? std::string{} : std::string(world.atoms().text(descriptor->name));
}

// --- Model ------------------------------------------------------------------

Value getModelPrimaryPart(const World& world, core::InstanceId id)
{
    const ModelComponent* model = world.models().find(id);
    if (model == nullptr || !model->primaryPart.valid())
        return Value{};
    return Value{model->primaryPart};
}

bool setModelPrimaryPart(World& world, core::InstanceId id, const Value& value)
{
    ModelComponent* model = world.models().find(id);
    if (model == nullptr)
        return false;
    if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr)
    {
        model->primaryPart = *reference;
        return true;
    }
    if (valueType(value) != ValueType::Nil)
        return false;
    model->primaryPart = core::InstanceId{};
    return true;
}

// --- Script -----------------------------------------------------------------

Value getScriptEnabled(const World& world, core::InstanceId id)
{
    const ScriptComponent* script = world.scripts().find(id);
    return script == nullptr ? Value{} : Value{script->enabled};
}

bool setScriptEnabled(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    ScriptComponent* script = world.scripts().find(id);
    if (flag == nullptr || script == nullptr)
        return false;
    script->enabled = *flag;
    return true;
}

// --- BasePart ---------------------------------------------------------------

Value getBasePartCFrame(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{part->cframe};
}

bool setBasePartCFrame(World& world, core::InstanceId id, const Value& value)
{
    const auto* cframe = std::get_if<core::CFrameD>(&value);
    PartComponent* part = writePart(world, id);
    if (cframe == nullptr || part == nullptr)
        return false;
    part->cframe = *cframe;
    return true;
}

// Position and Orientation are views of the CFrame, not storage of their own
// (see components.h). Two sources of truth for one transform is how a prefab
// comes to set both and contradict itself.
Value getBasePartPosition(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{core::toVec3(part->cframe.position)};
}

bool setBasePartPosition(World& world, core::InstanceId id, const Value& value)
{
    const auto* position = std::get_if<core::Vec3>(&value);
    PartComponent* part = writePart(world, id);
    if (position == nullptr || part == nullptr)
        return false;
    // Widened rather than replaced: the rotation is untouched, and the f32 the
    // script supplied becomes the f64 of record.
    part->cframe.position = core::toDVec3(*position);
    return true;
}

Value getBasePartOrientation(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    if (part == nullptr)
        return Value{};
    const core::Vec3 radians = core::toEulerYxz(part->cframe.rotation);
    // Degrees, YXZ, matching api-design.md §2.2's Orientation.
    return Value{core::Vec3{
        static_cast<f32>(static_cast<f64>(radians.x) * RadiansToDegrees),
        static_cast<f32>(static_cast<f64>(radians.y) * RadiansToDegrees),
        static_cast<f32>(static_cast<f64>(radians.z) * RadiansToDegrees)}};
}

bool setBasePartOrientation(World& world, core::InstanceId id, const Value& value)
{
    const auto* degrees = std::get_if<core::Vec3>(&value);
    PartComponent* part = writePart(world, id);
    if (degrees == nullptr || part == nullptr)
        return false;
    part->cframe.rotation = core::fromEulerYxz(core::Vec3{
        static_cast<f32>(static_cast<f64>(degrees->x) * DegreesToRadians),
        static_cast<f32>(static_cast<f64>(degrees->y) * DegreesToRadians),
        static_cast<f32>(static_cast<f64>(degrees->z) * DegreesToRadians)});
    return true;
}

Value getBasePartSize(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{part->size};
}

bool setBasePartSize(World& world, core::InstanceId id, const Value& value)
{
    const auto* size = std::get_if<core::Vec3>(&value);
    PartComponent* part = writePart(world, id);
    if (size == nullptr || part == nullptr)
        return false;
    part->size = *size;
    return true;
}

Value getBasePartColor(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{part->color};
}

bool setBasePartColor(World& world, core::InstanceId id, const Value& value)
{
    const auto* color = std::get_if<core::Color3>(&value);
    PartComponent* part = writePart(world, id);
    if (color == nullptr || part == nullptr)
        return false;
    // Not clamped: api-design.md §2.3 leaves the range open so an HDR value
    // survives a round trip through a property.
    part->color = *color;
    return true;
}

Value getBasePartTransparency(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{static_cast<f64>(part->transparency)};
}

bool setBasePartTransparency(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    PartComponent* part = writePart(world, id);
    if (number == nullptr || part == nullptr)
        return false;
    part->transparency = static_cast<f32>(*number);
    return true;
}

// --- Part -------------------------------------------------------------------

Value getPartShape(const World& world, core::InstanceId id)
{
    const PartComponent* part = readPart(world, id);
    return part == nullptr ? Value{} : Value{EnumValue{generated::PartShapeEnumId, part->shape}};
}

bool setPartShape(World& world, core::InstanceId id, const Value& value)
{
    const auto* item = std::get_if<EnumValue>(&value);
    PartComponent* part = writePart(world, id);
    if (item == nullptr || part == nullptr || item->enumId != generated::PartShapeEnumId)
        return false;
    // The id alone would let `Enum.PartShape` accept a number no item carries.
    // The registry is the only thing that knows the item list, and this is a
    // property write rather than a hot read, so the walk is affordable here in
    // a way it would not be in `getPartShape`.
    if (world.enums().findValue(item->enumId, item->value) == nullptr)
        return false;
    part->shape = item->value;
    return true;
}

// --- Services ---------------------------------------------------------------

Value getDataModelEngineVersion(const World& world, core::InstanceId)
{
    return world.engineState().engineVersion;
}

Value getDataModelLuauVersion(const World& world, core::InstanceId)
{
    return world.engineState().luauVersion;
}

Value getRunServiceSimTime(const World& world, core::InstanceId)
{
    return world.engineState().simTime;
}

Value getPhysicsServiceFixedTimestep(const World& world, core::InstanceId)
{
    return world.engineState().fixedTimestep;
}

Value getDebugServiceOverlayVisible(const World& world, core::InstanceId)
{
    return world.engineState().overlayVisible;
}

bool setDebugServiceOverlayVisible(World& world, core::InstanceId, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    world.engineState().overlayVisible = *flag;
    return true;
}

} // namespace luaug::scene::native
