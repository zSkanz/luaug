// The 2D layer's properties (post-v1 phase 3, docs/briefs/p3-2d-kickoff.md).
//
// Every write is checked here, where a refusal becomes a keyed error, rather
// than in the solver or the renderer, where it would be a body that leaves the
// world or a sprite of no size.
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <limits>
#include <string>
#include <variant>

#include "class_descriptors.gen.h"

namespace luaug::scene::native {
namespace {

[[nodiscard]] bool takeF32(const Value& value, f32& out) noexcept
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !std::isfinite(*number))
        return false;
    out = static_cast<f32>(*number);
    return true;
}

[[nodiscard]] bool takeVec2(const Value& value, core::Vec2& out) noexcept
{
    const auto* point = std::get_if<core::Vec2>(&value);
    if (point == nullptr || !std::isfinite(point->x) || !std::isfinite(point->y))
        return false;
    out = *point;
    return true;
}

// A layer is a whole number: a ZIndex of 1.5 would be a tie broken by a
// rounding nobody chose.
[[nodiscard]] bool takeInteger(const Value& value, i32& out) noexcept
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !std::isfinite(*number) || std::floor(*number) != *number ||
        *number < static_cast<f64>(std::numeric_limits<i32>::min()) ||
        *number > static_cast<f64>(std::numeric_limits<i32>::max()))
        return false;
    out = static_cast<i32>(*number);
    return true;
}

// A group that exists, as a `BasePart`'s must: a typo here is a floor
// everything falls through.
[[nodiscard]] bool takeGroup(const World& world, const Value& value, core::NameAtom& out)
{
    const auto* name = std::get_if<std::string>(&value);
    if (name == nullptr)
        return false;
    const core::NameAtom atom = world.atoms().lookup(*name);
    if (!atom.valid() || world.collisionGroups().find(atom) == CollisionGroups::kInvalid)
        return false;
    out = atom;
    return true;
}

} // namespace

// --- Component hooks --------------------------------------------------------

void attachPart2DComponents(World& world, core::InstanceId id)
{
    Part2DComponent part;
    part.collisionGroup = world.atoms().intern("Default");
    world.parts2d().add(id, part);
}

void detachPart2DComponents(World& world, core::InstanceId id)
{
    world.parts2d().remove(id);
}

void attachTilemap2DComponents(World& world, core::InstanceId id)
{
    Tilemap2DComponent tilemap;
    tilemap.collisionGroup = world.atoms().intern("Default");
    world.tilemaps2d().add(id, std::move(tilemap));
}

void detachTilemap2DComponents(World& world, core::InstanceId id)
{
    world.tilemaps2d().remove(id);
}

// Part2D.Position
Value getPart2DPosition(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->position};
}

bool setPart2DPosition(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(true))
        return false;
    c->position = next;
    return true;
}

// Part2D.Rotation
Value getPart2DRotation(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->rotation)};
}

bool setPart2DRotation(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(true))
        return false;
    c->rotation = next;
    return true;
}

// Part2D.Size
Value getPart2DSize(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->size};
}

bool setPart2DSize(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(next.x > 0.0f && next.y > 0.0f))
        return false;
    c->size = next;
    return true;
}

// Part2D.Shape
Value getPart2DShape(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{EnumValue{generated::Shape2DEnumId, c->shape}};
}

bool setPart2DShape(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* item = std::get_if<EnumValue>(&value);
    if (c == nullptr || item == nullptr || item->enumId != generated::Shape2DEnumId || item->value < 0 ||
        item->value > 2)
        return false;
    c->shape = item->value;
    return true;
}

// Part2D.Color
Value getPart2DColor(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->color};
}

bool setPart2DColor(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* color = std::get_if<core::Color3>(&value);
    if (c == nullptr || color == nullptr)
        return false;
    c->color = *color;
    return true;
}

// Part2D.Transparency
Value getPart2DTransparency(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->transparency)};
}

bool setPart2DTransparency(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next >= 0.0f && next <= 1.0f))
        return false;
    c->transparency = next;
    return true;
}

// Part2D.ZIndex
Value getPart2DZIndex(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->zIndex)};
}

bool setPart2DZIndex(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    i32 next = 0;
    if (c == nullptr || !takeInteger(value, next))
        return false;
    c->zIndex = next;
    return true;
}

// Part2D.FlipX
Value getPart2DFlipX(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->flipX};
}

bool setPart2DFlipX(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->flipX = *flag;
    return true;
}

// Part2D.FlipY
Value getPart2DFlipY(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->flipY};
}

bool setPart2DFlipY(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->flipY = *flag;
    return true;
}

// Part2D.Image
Value getPart2DImage(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{std::string(world.atoms().text(c->image))};
}

bool setPart2DImage(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* text = std::get_if<std::string>(&value);
    if (c == nullptr || text == nullptr)
        return false;
    c->image = world.atoms().intern(*text);
    return true;
}

// Part2D.ImageRectOffset
Value getPart2DImageRectOffset(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->imageRectOffset};
}

bool setPart2DImageRectOffset(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(next.x >= 0.0f && next.y >= 0.0f))
        return false;
    c->imageRectOffset = next;
    return true;
}

// Part2D.ImageRectSize
Value getPart2DImageRectSize(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->imageRectSize};
}

bool setPart2DImageRectSize(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(next.x >= 0.0f && next.y >= 0.0f))
        return false;
    c->imageRectSize = next;
    return true;
}

// Part2D.Filter
Value getPart2DFilter(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{EnumValue{generated::TextureFilterEnumId, c->filter}};
}

bool setPart2DFilter(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* item = std::get_if<EnumValue>(&value);
    if (c == nullptr || item == nullptr || item->enumId != generated::TextureFilterEnumId || item->value < 0 ||
        item->value > 1)
        return false;
    c->filter = item->value;
    return true;
}

// Part2D.Anchored
Value getPart2DAnchored(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->anchored};
}

bool setPart2DAnchored(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->anchored = *flag;
    return true;
}

// Part2D.CanCollide
Value getPart2DCanCollide(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->canCollide};
}

bool setPart2DCanCollide(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->canCollide = *flag;
    return true;
}

// Part2D.Sensor
Value getPart2DSensor(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->sensor};
}

bool setPart2DSensor(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->sensor = *flag;
    return true;
}

// Part2D.Density
Value getPart2DDensity(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->density)};
}

bool setPart2DDensity(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next > 0.0f))
        return false;
    c->density = next;
    return true;
}

// Part2D.Friction
Value getPart2DFriction(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->friction)};
}

bool setPart2DFriction(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next >= 0.0f))
        return false;
    c->friction = next;
    return true;
}

// Part2D.Elasticity
Value getPart2DElasticity(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->elasticity)};
}

bool setPart2DElasticity(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next >= 0.0f && next <= 1.0f))
        return false;
    c->elasticity = next;
    return true;
}

// Part2D.FixedRotation
Value getPart2DFixedRotation(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->fixedRotation};
}

bool setPart2DFixedRotation(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->fixedRotation = *flag;
    return true;
}

// Part2D.GravityScale
Value getPart2DGravityScale(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->gravityScale)};
}

bool setPart2DGravityScale(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(true))
        return false;
    c->gravityScale = next;
    return true;
}

// Part2D.Velocity
Value getPart2DVelocity(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{c->velocity};
}

bool setPart2DVelocity(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(true))
        return false;
    c->velocity = next;
    return true;
}

// Part2D.AngularVelocity
Value getPart2DAngularVelocity(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->angularVelocity)};
}

bool setPart2DAngularVelocity(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(true))
        return false;
    c->angularVelocity = next;
    return true;
}

// Part2D.CollisionGroup
Value getPart2DCollisionGroup(const World& world, core::InstanceId id)
{
    const Part2DComponent* c = world.parts2d().find(id);
    return c == nullptr ? Value{} : Value{std::string(world.atoms().text(c->collisionGroup))};
}

bool setPart2DCollisionGroup(World& world, core::InstanceId id, const Value& value)
{
    Part2DComponent* c = world.parts2d().find(id);
    return c != nullptr && takeGroup(world, value, c->collisionGroup);
}

// Tilemap2D.Position
Value getTilemap2DPosition(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{c->position};
}

bool setTilemap2DPosition(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(true))
        return false;
    c->position = next;
    return true;
}

// Tilemap2D.CellSize
Value getTilemap2DCellSize(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->cellSize)};
}

bool setTilemap2DCellSize(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next > 0.0f))
        return false;
    c->cellSize = next;
    return true;
}

// Tilemap2D.Tileset
Value getTilemap2DTileset(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{std::string(world.atoms().text(c->tileset))};
}

bool setTilemap2DTileset(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    const auto* text = std::get_if<std::string>(&value);
    if (c == nullptr || text == nullptr)
        return false;
    c->tileset = world.atoms().intern(*text);
    return true;
}

// Tilemap2D.TileSize
Value getTilemap2DTileSize(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{c->tileSize};
}

bool setTilemap2DTileSize(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    core::Vec2 next;
    if (c == nullptr || !takeVec2(value, next) || !(next.x > 0.0f && next.y > 0.0f))
        return false;
    c->tileSize = next;
    return true;
}

// Tilemap2D.ZIndex
Value getTilemap2DZIndex(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->zIndex)};
}

bool setTilemap2DZIndex(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    i32 next = 0;
    if (c == nullptr || !takeInteger(value, next))
        return false;
    c->zIndex = next;
    return true;
}

// Tilemap2D.Color
Value getTilemap2DColor(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{c->color};
}

bool setTilemap2DColor(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    const auto* color = std::get_if<core::Color3>(&value);
    if (c == nullptr || color == nullptr)
        return false;
    c->color = *color;
    return true;
}

// Tilemap2D.Filter
Value getTilemap2DFilter(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{EnumValue{generated::TextureFilterEnumId, c->filter}};
}

bool setTilemap2DFilter(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    const auto* item = std::get_if<EnumValue>(&value);
    if (c == nullptr || item == nullptr || item->enumId != generated::TextureFilterEnumId || item->value < 0 ||
        item->value > 1)
        return false;
    c->filter = item->value;
    return true;
}

// Tilemap2D.Collides
Value getTilemap2DCollides(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{c->collides};
}

bool setTilemap2DCollides(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    const auto* flag = std::get_if<bool>(&value);
    if (c == nullptr || flag == nullptr)
        return false;
    c->collides = *flag;
    return true;
}

// Tilemap2D.Friction
Value getTilemap2DFriction(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->friction)};
}

bool setTilemap2DFriction(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !(next >= 0.0f))
        return false;
    c->friction = next;
    return true;
}

// Tilemap2D.CollisionGroup
Value getTilemap2DCollisionGroup(const World& world, core::InstanceId id)
{
    const Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c == nullptr ? Value{} : Value{std::string(world.atoms().text(c->collisionGroup))};
}

bool setTilemap2DCollisionGroup(World& world, core::InstanceId id, const Value& value)
{
    Tilemap2DComponent* c = world.tilemaps2d().find(id);
    return c != nullptr && takeGroup(world, value, c->collisionGroup);
}

} // namespace luaug::scene::native
