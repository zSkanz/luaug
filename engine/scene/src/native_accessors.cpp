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
#include "luaug/scene/world.h"

#include <cmath>
#include <string>
#include <string_view>

#include "../generated/class_descriptors.gen.h"

namespace luaug::scene::native {
namespace {

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

[[nodiscard]] const RigidBodyComponent* readBody(const World& world, core::InstanceId id) noexcept
{
    return world.rigidBodies().find(id);
}

[[nodiscard]] RigidBodyComponent* writeBody(World& world, core::InstanceId id) noexcept
{
    return world.rigidBodies().find(id);
}

// Every physics scalar a script writes is finite and in a stated range. A NaN
// reaching the solver is not a wrong number, it is a body that leaves the world
// and takes its contact island with it -- so the refusal happens here, where it
// becomes a keyed error, rather than three layers down where it becomes a
// missing part.
[[nodiscard]] bool finite(f64 value) noexcept
{
    return std::isfinite(value);
}

// An overload rather than one function taking f64: a `vector` component is f32,
// and letting it promote is a warning Clang treats as an error here -- correctly,
// because a silent widening is how a precision decision stops being one.
[[nodiscard]] bool finite(f32 value) noexcept
{
    return std::isfinite(value);
}

} // namespace

// --- Component hooks --------------------------------------------------------
//
// One pair per class that *declares* a component. `World::create` walks the
// ancestry root-first and calls each, so `Part` gets `PartComponent` from
// `BasePart`'s pair and declares none of its own -- declaring it twice would
// attach twice.

void attachPVComponents(World& world, core::InstanceId id)
{
    world.pvInstances().add(id, PVComponent{});
}

void detachPVComponents(World& world, core::InstanceId id)
{
    world.pvInstances().remove(id);
}

void attachPartComponents(World& world, core::InstanceId id)
{
    world.parts().add(id, PartComponent{});
    // A `BasePart` has both or neither: the two halves are split by who reads
    // them, not by which parts have them.
    RigidBodyComponent body;
    body.collisionGroup = world.collisionGroups().nameAt(CollisionGroups::kDefault);
    world.rigidBodies().add(id, body);
}

void detachPartComponents(World& world, core::InstanceId id)
{
    world.parts().remove(id);
    world.rigidBodies().remove(id);
}

void attachCharacterBodyComponents(World& world, core::InstanceId id)
{
    world.characterBodies().add(id, CharacterBodyComponent{});
}

void detachCharacterBodyComponents(World& world, core::InstanceId id)
{
    world.characterBodies().remove(id);
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

// --- Workspace --------------------------------------------------------------

Value getWorkspaceCurrentCamera(const World& world, core::InstanceId id)
{
    const WorkspaceComponent* workspace = world.workspaces().find(id);
    if (workspace == nullptr || !world.alive(workspace->currentCamera))
        return Value{};
    return Value{workspace->currentCamera};
}

bool setWorkspaceCurrentCamera(World& world, core::InstanceId id, const Value& value)
{
    WorkspaceComponent* workspace = world.workspaces().find(id);
    if (workspace == nullptr)
        return false;
    if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr) {
        // The property is typed `Camera?`, and the type definitions enforce that
        // for anyone who runs the analyzer. This is the runtime half, because a
        // Part assigned here would otherwise be a camera that renders a view
        // nobody can explain.
        const ClassId cameraClass = world.classes().findId(world.atoms().lookup("Camera"));
        // `lookup` rather than `intern`: this is a hot-ish write path and
        // interning a name that must already exist would mint an atom on the one
        // path where the answer is "no such class". An engine built without the
        // render module registers no Camera, and then nothing may be assigned
        // here -- which is correct, not a failure to handle.
        if (cameraClass == InvalidClass || !world.isA(*reference, cameraClass))
            return false;
        workspace->currentCamera = *reference;
        return true;
    }
    if (valueType(value) != ValueType::Nil)
        return false;
    // Nil renders nothing rather than falling back to a camera the engine
    // invented: a view nobody asked for is harder to debug than a black frame.
    workspace->currentCamera = core::InstanceId{};
    return true;
}

Value getWorkspaceGravity(const World& world, core::InstanceId id)
{
    const WorkspaceComponent* workspace = world.workspaces().find(id);
    return workspace == nullptr ? Value{} : Value{workspace->gravity};
}

bool setWorkspaceGravity(World& world, core::InstanceId id, const Value& value)
{
    const auto* gravity = std::get_if<core::Vec3>(&value);
    WorkspaceComponent* workspace = world.workspaces().find(id);
    if (gravity == nullptr || workspace == nullptr)
        return false;
    if (!finite(gravity->x) || !finite(gravity->y) || !finite(gravity->z))
        return false;
    workspace->gravity = *gravity;
    return true;
}

void attachWorkspaceComponents(World& world, core::InstanceId id)
{
    world.workspaces().add(id, WorkspaceComponent{});
}

void detachWorkspaceComponents(World& world, core::InstanceId id)
{
    world.workspaces().remove(id);
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
    if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr) {
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

// --- PVInstance --------------------------------------------------------------

Value getPVInstancePivotOffset(const World& world, core::InstanceId id)
{
    const PVComponent* pv = world.pvInstances().find(id);
    return pv == nullptr ? Value{} : Value{pv->pivotOffset};
}

bool setPVInstancePivotOffset(World& world, core::InstanceId id, const Value& value)
{
    const auto* cframe = std::get_if<core::CFrameD>(&value);
    PVComponent* pv = world.pvInstances().find(id);
    if (cframe == nullptr || pv == nullptr)
        return false;
    pv->pivotOffset = *cframe;
    return true;
}

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
    return Value{core::Vec3{static_cast<f32>(static_cast<f64>(radians.x) * RadiansToDegrees),
                            static_cast<f32>(static_cast<f64>(radians.y) * RadiansToDegrees),
                            static_cast<f32>(static_cast<f64>(radians.z) * RadiansToDegrees)}};
}

bool setBasePartOrientation(World& world, core::InstanceId id, const Value& value)
{
    const auto* degrees = std::get_if<core::Vec3>(&value);
    PartComponent* part = writePart(world, id);
    if (degrees == nullptr || part == nullptr)
        return false;
    part->cframe.rotation =
        core::fromEulerYxz(core::Vec3{static_cast<f32>(static_cast<f64>(degrees->x) * DegreesToRadians),
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

// --- BasePart, the physical half (M5) ---------------------------------------

Value getBasePartAnchored(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{body->anchored};
}

bool setBasePartAnchored(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (flag == nullptr || body == nullptr)
        return false;
    body->anchored = *flag;
    return true;
}

Value getBasePartCanCollide(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{body->canCollide};
}

bool setBasePartCanCollide(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (flag == nullptr || body == nullptr)
        return false;
    body->canCollide = *flag;
    return true;
}

Value getBasePartCanQuery(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{body->canQuery};
}

bool setBasePartCanQuery(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (flag == nullptr || body == nullptr)
        return false;
    body->canQuery = *flag;
    return true;
}

Value getBasePartCollisionGroup(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    if (body == nullptr)
        return Value{};
    return Value{std::string{world.atoms().text(body->collisionGroup)}};
}

bool setBasePartCollisionGroup(World& world, core::InstanceId id, const Value& value)
{
    const auto* name = std::get_if<std::string>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (name == nullptr || body == nullptr)
        return false;

    // An unregistered name is refused rather than folded into Default. The
    // failure mode of a typo here is a wall players walk through, which is
    // expensive to find and cheap to refuse -- and the property's Doc says so.
    const core::NameAtom atom = world.atoms().lookup(*name);
    if (!atom.valid() || world.collisionGroups().find(atom) == CollisionGroups::kInvalid)
        return false;
    body->collisionGroup = atom;
    return true;
}

Value getBasePartFriction(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{static_cast<f64>(body->friction)};
}

bool setBasePartFriction(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (number == nullptr || body == nullptr || !finite(*number) || *number < 0.0)
        return false;
    body->friction = static_cast<f32>(*number);
    return true;
}

Value getBasePartRestitution(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{static_cast<f64>(body->restitution)};
}

bool setBasePartRestitution(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    if (number == nullptr || body == nullptr || !finite(*number) || *number < 0.0 || *number > 1.0)
        return false;
    body->restitution = static_cast<f32>(*number);
    return true;
}

Value getBasePartDensity(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{static_cast<f64>(body->density)};
}

bool setBasePartDensity(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    RigidBodyComponent* body = writeBody(world, id);
    // Zero is refused rather than clamped: a massless body is not a light one,
    // it is a division the solver cannot do.
    if (number == nullptr || body == nullptr || !finite(*number) || *number <= 0.0)
        return false;
    body->density = static_cast<f32>(*number);
    return true;
}

Value getBasePartLinearVelocity(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{body->linearVelocity};
}

Value getBasePartAngularVelocity(const World& world, core::InstanceId id)
{
    const RigidBodyComponent* body = readBody(world, id);
    return body == nullptr ? Value{} : Value{body->angularVelocity};
}

// --- CharacterBody ----------------------------------------------------------

Value getCharacterBodyWalkSpeed(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    return character == nullptr ? Value{} : Value{static_cast<f64>(character->walkSpeed)};
}

bool setCharacterBodyWalkSpeed(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    CharacterBodyComponent* character = world.characterBodies().find(id);
    if (number == nullptr || character == nullptr || !finite(*number) || *number < 0.0)
        return false;
    character->walkSpeed = static_cast<f32>(*number);
    return true;
}

Value getCharacterBodyJumpSpeed(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    return character == nullptr ? Value{} : Value{static_cast<f64>(character->jumpSpeed)};
}

bool setCharacterBodyJumpSpeed(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    CharacterBodyComponent* character = world.characterBodies().find(id);
    if (number == nullptr || character == nullptr || !finite(*number) || *number < 0.0)
        return false;
    character->jumpSpeed = static_cast<f32>(*number);
    return true;
}

Value getCharacterBodyMaxSlopeAngle(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    return character == nullptr ? Value{} : Value{static_cast<f64>(character->maxSlopeAngle)};
}

bool setCharacterBodyMaxSlopeAngle(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    CharacterBodyComponent* character = world.characterBodies().find(id);
    // Ninety degrees is a wall; past it the word "slope" stops meaning
    // anything, and the controller would treat every surface as ground.
    if (number == nullptr || character == nullptr || !finite(*number) || *number < 0.0 || *number >= 90.0)
        return false;
    character->maxSlopeAngle = static_cast<f32>(*number);
    return true;
}

Value getCharacterBodyAutoStepHeight(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    return character == nullptr ? Value{} : Value{static_cast<f64>(character->autoStepHeight)};
}

bool setCharacterBodyAutoStepHeight(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    CharacterBodyComponent* character = world.characterBodies().find(id);
    if (number == nullptr || character == nullptr || !finite(*number) || *number < 0.0)
        return false;
    character->autoStepHeight = static_cast<f32>(*number);
    return true;
}

Value getCharacterBodyGrounded(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    return character == nullptr ? Value{} : Value{character->grounded};
}

Value getCharacterBodyState(const World& world, core::InstanceId id)
{
    const CharacterBodyComponent* character = world.characterBodies().find(id);
    if (character == nullptr)
        return Value{};
    return Value{EnumValue{generated::CharacterStateEnumId, character->state}};
}

// --- Weld and WeldConstraint (M5) --------------------------------------------
//
// Both classes share `WeldComponent`; `captures` is the whole difference.
//
// The cycle check lives in the two part setters rather than in the resolver,
// and that is the same choice `Parent` makes: a graph that cannot contain a
// cycle needs no cycle handling at the point where order matters, and the write
// that would create one is the only place a caller can be told which write it
// was.

[[nodiscard]] const WeldComponent* readWeld(const World& world, core::InstanceId id) noexcept
{
    return world.welds().find(id);
}

[[nodiscard]] WeldComponent* writeWeld(World& world, core::InstanceId id) noexcept
{
    return world.welds().find(id);
}

// Walks the anchor chain up from `part`, following every weld whose driven part
// it is. True when `target` is reachable, which is what makes the proposed weld
// a cycle.
//
// Linear in the number of welds and bounded by it, because a chain that
// revisited an instance would already be a cycle -- and no cycle can exist,
// because this check refuses the write that would make one.
[[nodiscard]] bool weldReaches(const World& world, core::InstanceId part, core::InstanceId target,
                               core::InstanceId ignoreWeld)
{
    core::InstanceId current = part;
    for (usize guard = 0; guard <= world.welds().size(); ++guard) {
        if (!current.valid())
            return false;
        if (current == target)
            return true;

        core::InstanceId next;
        world.welds().forEach([&](core::InstanceId weldId, const WeldComponent& weld) {
            if (weldId == ignoreWeld || !weld.enabled)
                return;
            if (weld.part1 == current)
                next = weld.part0;
        });
        current = next;
    }
    return true;
}

[[nodiscard]] bool setWeldPart(World& world, core::InstanceId id, const Value& value, bool isPart0)
{
    WeldComponent* weld = writeWeld(world, id);
    if (weld == nullptr)
        return false;

    core::InstanceId part;
    if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr) {
        if (!world.alive(*reference))
            return false;
        part = *reference;
    }
    else if (valueType(value) != ValueType::Nil) {
        return false;
    }

    const core::InstanceId part0 = isPart0 ? part : weld->part0;
    const core::InstanceId part1 = isPart0 ? weld->part1 : part;

    // A part welded to itself is the shortest cycle there is, and the one a
    // script writes by assigning the same variable twice.
    if (part0.valid() && part0 == part1)
        return false;
    if (part0.valid() && part1.valid() && weldReaches(world, part0, part1, id))
        return false;

    if (isPart0)
        weld->part0 = part;
    else
        weld->part1 = part;

    // A constraint captures afresh whenever its pair changes: the transform it
    // was holding described two parts, and one of them is no longer one of
    // these two.
    if (weld->captures)
        weld->captured = false;
    return true;
}

Value getWeldPart0(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    return weld == nullptr ? Value{} : Value{weld->part0};
}

bool setWeldPart0(World& world, core::InstanceId id, const Value& value)
{
    return setWeldPart(world, id, value, true);
}

Value getWeldPart1(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    return weld == nullptr ? Value{} : Value{weld->part1};
}

bool setWeldPart1(World& world, core::InstanceId id, const Value& value)
{
    return setWeldPart(world, id, value, false);
}

Value getWeldC0(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    return weld == nullptr ? Value{} : Value{weld->c0};
}

bool setWeldC0(World& world, core::InstanceId id, const Value& value)
{
    const auto* cframe = std::get_if<core::CFrameD>(&value);
    WeldComponent* weld = writeWeld(world, id);
    if (cframe == nullptr || weld == nullptr)
        return false;
    weld->c0 = *cframe;
    return true;
}

Value getWeldC1(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    return weld == nullptr ? Value{} : Value{weld->c1};
}

bool setWeldC1(World& world, core::InstanceId id, const Value& value)
{
    const auto* cframe = std::get_if<core::CFrameD>(&value);
    WeldComponent* weld = writeWeld(world, id);
    if (cframe == nullptr || weld == nullptr)
        return false;
    weld->c1 = *cframe;
    return true;
}

Value getWeldEnabled(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    return weld == nullptr ? Value{} : Value{weld->enabled};
}

bool setWeldEnabled(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    WeldComponent* weld = writeWeld(world, id);
    if (flag == nullptr || weld == nullptr)
        return false;
    // Off to on re-captures, which is how a part is re-welded somewhere else:
    // move it, then enable.
    if (*flag && !weld->enabled && weld->captures)
        weld->captured = false;
    weld->enabled = *flag;
    return true;
}

void attachWeldComponents(World& world, core::InstanceId id)
{
    world.welds().add(id, WeldComponent{});
}

void detachWeldComponents(World& world, core::InstanceId id)
{
    world.welds().remove(id);
}

void attachWeldConstraintComponents(World& world, core::InstanceId id)
{
    WeldComponent weld;
    weld.captures = true;
    world.welds().add(id, weld);
}

void detachWeldConstraintComponents(World& world, core::InstanceId id)
{
    world.welds().remove(id);
}

Value getWeldConstraintPart0(const World& world, core::InstanceId id)
{
    return getWeldPart0(world, id);
}

bool setWeldConstraintPart0(World& world, core::InstanceId id, const Value& value)
{
    return setWeldPart0(world, id, value);
}

Value getWeldConstraintPart1(const World& world, core::InstanceId id)
{
    return getWeldPart1(world, id);
}

bool setWeldConstraintPart1(World& world, core::InstanceId id, const Value& value)
{
    return setWeldPart1(world, id, value);
}

Value getWeldConstraintEnabled(const World& world, core::InstanceId id)
{
    return getWeldEnabled(world, id);
}

bool setWeldConstraintEnabled(World& world, core::InstanceId id, const Value& value)
{
    return setWeldEnabled(world, id, value);
}

Value getWeldConstraintActive(const World& world, core::InstanceId id)
{
    const WeldComponent* weld = readWeld(world, id);
    if (weld == nullptr)
        return Value{};
    // "Enabled, with both parts set and both in the world." Whether they are in
    // the world is the tree's answer and the mirror's to act on; what this
    // reports is everything a script can check for itself.
    return Value{weld->enabled && world.alive(weld->part0) && world.alive(weld->part1)};
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
    // The REQUESTED value, so a write round-trips immediately. What the
    // scheduler is running on is `fixedTimestep`, which it copies from here at
    // the next safe point -- see the property's Doc and `EngineState`.
    return world.engineState().requestedFixedTimestep;
}

bool setPhysicsServiceFixedTimestep(World& world, core::InstanceId, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !finite(*number))
        return false;
    // 30 Hz to 240 Hz, the range architecture.md §3 states. Outside it the
    // guarantees expressed against the tick stop meaning anything: at 10 Hz a
    // falling part passes through a floor, and at 1000 Hz the accumulator's
    // four-step clamp turns real time into slow motion.
    constexpr f64 Fastest = 1.0 / 240.0;
    constexpr f64 Slowest = 1.0 / 30.0;
    if (*number < Fastest || *number > Slowest)
        return false;
    world.engineState().requestedFixedTimestep = *number;
    return true;
}

Value getStreamingServiceEnabled(const World& world, core::InstanceId)
{
    return world.engineState().streamingEnabled;
}

bool setStreamingServiceEnabled(World& world, core::InstanceId, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    world.engineState().streamingEnabled = *flag;
    return true;
}

Value getStreamingServiceLoadRadius(const World& world, core::InstanceId)
{
    return world.engineState().streamingLoadRadius;
}

bool setStreamingServiceLoadRadius(World& world, core::InstanceId, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    // Strictly positive: a radius of zero is a world that never loads anything,
    // which reads as "streaming is broken" rather than as "you asked for
    // nothing".
    if (number == nullptr || !finite(*number) || *number <= 0.0)
        return false;
    world.engineState().streamingLoadRadius = *number;
    return true;
}

Value getStreamingServiceMinRadius(const World& world, core::InstanceId)
{
    return world.engineState().streamingMinRadius;
}

bool setStreamingServiceMinRadius(World& world, core::InstanceId, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !finite(*number) || *number <= 0.0)
        return false;
    // Deliberately NOT clamped against `LoadRadius`. The two are written in
    // whichever order a script happens to write them, and refusing the first
    // write because the second has not happened yet is a property that depends
    // on statement order. The manager takes the smaller of the two as the
    // must-have ring, which is the same answer without the trap.
    world.engineState().streamingMinRadius = *number;
    return true;
}

Value getStreamingServicePauseOutsideLoadedArea(const World& world, core::InstanceId)
{
    return world.engineState().streamingPauseOutsideLoadedArea;
}

bool setStreamingServicePauseOutsideLoadedArea(World& world, core::InstanceId, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    world.engineState().streamingPauseOutsideLoadedArea = *flag;
    return true;
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
