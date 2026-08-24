// The hand-written half of the render module's classes (architecture.md §4).
//
// The generator emits descriptors and declares these; nothing here knows what a
// descriptor is. Each pair is the only code that touches its component, which
// is what lets a property's storage move without the API definition changing.
//
// Every getter answers `Value{}` -- nil -- for an instance whose component is
// missing, rather than asserting. A property read on an instance that is being
// torn down is ordinary, and the alternative is a crash on a race the tree
// makes legal.
//
// Every setter returns false rather than raising. `scene` has no error
// formatting (that lives above it), so the caller turns a false into the
// property's own `errKeyOnInvalidSet`.
#include "luaug/core/math.h"
#include "luaug/render/lighting.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <string>
#include <variant>

#include "../generated/class_descriptors.gen.h"

namespace luaug::render::native {
namespace {

using core::f32;
using core::f64;
using scene::Value;

// The read/write pairs below are one line each and exist so the null check is
// written once per component rather than once per accessor. `find` on a pool
// the instance does not belong to returns null, which is the case every getter
// has to answer nil for.
[[nodiscard]] const scene::MeshPartComponent* readMeshPart(const scene::World& world, core::InstanceId id)
{
    return world.meshParts().find(id);
}

[[nodiscard]] scene::MeshPartComponent* writeMeshPart(scene::World& world, core::InstanceId id)
{
    return world.meshParts().find(id);
}

[[nodiscard]] const scene::CameraComponent* readCamera(const scene::World& world, core::InstanceId id)
{
    return world.cameras().find(id);
}

[[nodiscard]] scene::CameraComponent* writeCamera(scene::World& world, core::InstanceId id)
{
    return world.cameras().find(id);
}

[[nodiscard]] const scene::PointLightComponent* readPointLight(const scene::World& world, core::InstanceId id)
{
    return world.pointLights().find(id);
}

[[nodiscard]] scene::PointLightComponent* writePointLight(scene::World& world, core::InstanceId id)
{
    return world.pointLights().find(id);
}

[[nodiscard]] const scene::SpotLightComponent* readSpotLight(const scene::World& world, core::InstanceId id)
{
    return world.spotLights().find(id);
}

[[nodiscard]] scene::SpotLightComponent* writeSpotLight(scene::World& world, core::InstanceId id)
{
    return world.spotLights().find(id);
}

[[nodiscard]] const scene::LightingComponent* readLighting(const scene::World& world, core::InstanceId id)
{
    return world.lighting().find(id);
}

[[nodiscard]] scene::LightingComponent* writeLighting(scene::World& world, core::InstanceId id)
{
    return world.lighting().find(id);
}

// A number property that must stay finite and positive. Rejecting rather than
// clamping: a NaN field of view produces a projection matrix of NaNs and a black
// frame with no message, and the write that caused it is long gone by then.
[[nodiscard]] bool takePositive(const Value& value, f32& out) noexcept
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !std::isfinite(*number) || *number <= 0.0)
        return false;
    out = static_cast<f32>(*number);
    return true;
}

// A number property that may be any finite value, including zero and negatives.
[[nodiscard]] bool takeFinite(const Value& value, f32& out) noexcept
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !std::isfinite(*number))
        return false;
    out = static_cast<f32>(*number);
    return true;
}

} // namespace

// --- MeshPart ---------------------------------------------------------------

Value getMeshPartMeshContent(const scene::World& world, core::InstanceId id)
{
    const scene::MeshPartComponent* mesh = readMeshPart(world, id);
    if (mesh == nullptr)
        return Value{};
    return std::string(world.atoms().text(mesh->meshContent));
}

bool setMeshPartMeshContent(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* text = std::get_if<std::string>(&value);
    scene::MeshPartComponent* mesh = writeMeshPart(world, id);
    if (text == nullptr || mesh == nullptr)
        return false;
    // Interned, not resolved. Whether the file loads is the renderer's problem
    // and a later one; reading the property back must give what was written
    // even when it does not.
    mesh->meshContent = world.atoms().intern(*text);
    return true;
}

Value getMeshPartCollisionFidelity(const scene::World& world, core::InstanceId id)
{
    const scene::MeshPartComponent* mesh = readMeshPart(world, id);
    if (mesh == nullptr)
        return Value{};
    return Value{scene::EnumValue{generated::CollisionFidelityEnumId, mesh->collisionFidelity}};
}

bool setMeshPartCollisionFidelity(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* item = std::get_if<scene::EnumValue>(&value);
    scene::MeshPartComponent* mesh = writeMeshPart(world, id);
    if (item == nullptr || mesh == nullptr || item->enumId != generated::CollisionFidelityEnumId)
        return false;
    if (world.enums().findValue(item->enumId, item->value) == nullptr)
        return false;
    // Stored as written, including `Precise`, which this release collides as a
    // hull. A value that silently became another value is worse than one that
    // reads back what was asked for and says what it did (the property's Doc).
    mesh->collisionFidelity = item->value;
    return true;
}

void attachMeshPartComponents(scene::World& world, core::InstanceId id)
{
    world.meshParts().add(id, scene::MeshPartComponent{});
}

void detachMeshPartComponents(scene::World& world, core::InstanceId id)
{
    world.meshParts().remove(id);
}

// --- Camera -----------------------------------------------------------------

Value getCameraCFrame(const scene::World& world, core::InstanceId id)
{
    const scene::CameraComponent* camera = readCamera(world, id);
    return camera == nullptr ? Value{} : Value{camera->cframe};
}

bool setCameraCFrame(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* cframe = std::get_if<core::CFrameD>(&value);
    scene::CameraComponent* camera = writeCamera(world, id);
    if (cframe == nullptr || camera == nullptr)
        return false;
    camera->cframe = *cframe;
    return true;
}

Value getCameraFieldOfView(const scene::World& world, core::InstanceId id)
{
    const scene::CameraComponent* camera = readCamera(world, id);
    return camera == nullptr ? Value{} : Value{static_cast<f64>(camera->fieldOfView)};
}

bool setCameraFieldOfView(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::CameraComponent* camera = writeCamera(world, id);
    if (camera == nullptr)
        return false;
    f32 degrees = 0.0f;
    // The open interval matters at both ends: zero collapses the projection and
    // 180 sends the near plane to infinity. Both render nothing, silently.
    if (!takePositive(value, degrees) || degrees >= 180.0f)
        return false;
    camera->fieldOfView = degrees;
    return true;
}

Value getCameraNearPlane(const scene::World& world, core::InstanceId id)
{
    const scene::CameraComponent* camera = readCamera(world, id);
    return camera == nullptr ? Value{} : Value{static_cast<f64>(camera->nearPlane)};
}

bool setCameraNearPlane(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::CameraComponent* camera = writeCamera(world, id);
    if (camera == nullptr)
        return false;
    f32 distance = 0.0f;
    if (!takePositive(value, distance))
        return false;
    camera->nearPlane = distance;
    return true;
}

Value getCameraFarPlane(const scene::World& world, core::InstanceId id)
{
    const scene::CameraComponent* camera = readCamera(world, id);
    return camera == nullptr ? Value{} : Value{static_cast<f64>(camera->farPlane)};
}

bool setCameraFarPlane(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::CameraComponent* camera = writeCamera(world, id);
    if (camera == nullptr)
        return false;
    f32 distance = 0.0f;
    if (!takePositive(value, distance))
        return false;
    camera->farPlane = distance;
    return true;
}

void attachCameraComponents(scene::World& world, core::InstanceId id)
{
    world.cameras().add(id, scene::CameraComponent{});
}

void detachCameraComponents(scene::World& world, core::InstanceId id)
{
    world.cameras().remove(id);
}

// --- PointLight -------------------------------------------------------------

Value getPointLightColor(const scene::World& world, core::InstanceId id)
{
    const scene::PointLightComponent* light = readPointLight(world, id);
    return light == nullptr ? Value{} : Value{light->color};
}

bool setPointLightColor(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* color = std::get_if<core::Color3>(&value);
    scene::PointLightComponent* light = writePointLight(world, id);
    if (color == nullptr || light == nullptr)
        return false;
    light->color = *color;
    return true;
}

Value getPointLightBrightness(const scene::World& world, core::InstanceId id)
{
    const scene::PointLightComponent* light = readPointLight(world, id);
    return light == nullptr ? Value{} : Value{static_cast<f64>(light->brightness)};
}

bool setPointLightBrightness(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::PointLightComponent* light = writePointLight(world, id);
    if (light == nullptr)
        return false;
    // Zero is legal and is how a light is turned off without destroying it;
    // negative is not, because it would subtract light.
    f32 brightness = 0.0f;
    if (!takeFinite(value, brightness) || brightness < 0.0f)
        return false;
    light->brightness = brightness;
    return true;
}

Value getPointLightRange(const scene::World& world, core::InstanceId id)
{
    const scene::PointLightComponent* light = readPointLight(world, id);
    return light == nullptr ? Value{} : Value{static_cast<f64>(light->range)};
}

bool setPointLightRange(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::PointLightComponent* light = writePointLight(world, id);
    if (light == nullptr)
        return false;
    f32 range = 0.0f;
    if (!takeFinite(value, range) || range < 0.0f)
        return false;
    light->range = range;
    return true;
}

Value getPointLightEnabled(const scene::World& world, core::InstanceId id)
{
    const scene::PointLightComponent* light = readPointLight(world, id);
    return light == nullptr ? Value{} : Value{light->enabled};
}

bool setPointLightEnabled(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::PointLightComponent* light = writePointLight(world, id);
    if (flag == nullptr || light == nullptr)
        return false;
    light->enabled = *flag;
    return true;
}

Value getPointLightShadows(const scene::World& world, core::InstanceId id)
{
    const scene::PointLightComponent* light = readPointLight(world, id);
    return light == nullptr ? Value{} : Value{light->shadows};
}

bool setPointLightShadows(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::PointLightComponent* light = writePointLight(world, id);
    if (flag == nullptr || light == nullptr)
        return false;
    // Stored and reported faithfully while this release casts shadows from the
    // sun alone. A property that round-trips is honest; one that silently reads
    // back false would be the lie the IDL's own header warns about.
    light->shadows = *flag;
    return true;
}

void attachPointLightComponents(scene::World& world, core::InstanceId id)
{
    world.pointLights().add(id, scene::PointLightComponent{});
}

void detachPointLightComponents(scene::World& world, core::InstanceId id)
{
    world.pointLights().remove(id);
}

// --- SpotLight --------------------------------------------------------------

Value getSpotLightColor(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{light->color};
}

bool setSpotLightColor(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* color = std::get_if<core::Color3>(&value);
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (color == nullptr || light == nullptr)
        return false;
    light->color = *color;
    return true;
}

Value getSpotLightBrightness(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{static_cast<f64>(light->brightness)};
}

bool setSpotLightBrightness(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (light == nullptr)
        return false;
    f32 brightness = 0.0f;
    if (!takeFinite(value, brightness) || brightness < 0.0f)
        return false;
    light->brightness = brightness;
    return true;
}

Value getSpotLightRange(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{static_cast<f64>(light->range)};
}

bool setSpotLightRange(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (light == nullptr)
        return false;
    f32 range = 0.0f;
    if (!takeFinite(value, range) || range < 0.0f)
        return false;
    light->range = range;
    return true;
}

Value getSpotLightAngle(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{static_cast<f64>(light->angle)};
}

bool setSpotLightAngle(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (light == nullptr)
        return false;
    f32 angle = 0.0f;
    // The full cone width, so 180 is a hemisphere and anything at or beyond it
    // is no longer a cone.
    if (!takePositive(value, angle) || angle >= 180.0f)
        return false;
    light->angle = angle;
    return true;
}

Value getSpotLightEnabled(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{light->enabled};
}

bool setSpotLightEnabled(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (flag == nullptr || light == nullptr)
        return false;
    light->enabled = *flag;
    return true;
}

Value getSpotLightShadows(const scene::World& world, core::InstanceId id)
{
    const scene::SpotLightComponent* light = readSpotLight(world, id);
    return light == nullptr ? Value{} : Value{light->shadows};
}

bool setSpotLightShadows(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::SpotLightComponent* light = writeSpotLight(world, id);
    if (flag == nullptr || light == nullptr)
        return false;
    light->shadows = *flag;
    return true;
}

void attachSpotLightComponents(scene::World& world, core::InstanceId id)
{
    world.spotLights().add(id, scene::SpotLightComponent{});
}

void detachSpotLightComponents(scene::World& world, core::InstanceId id)
{
    world.spotLights().remove(id);
}

// --- Lighting ---------------------------------------------------------------

Value getLightingClockTime(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->clockTime)};
}

bool setLightingClockTime(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 hours = 0.0f;
    if (!takeFinite(value, hours))
        return false;
    // Wraps rather than clamps, so `ClockTime += dt` never has to check. A
    // clamp would make midnight a wall the sun stops against.
    hours = std::fmod(hours, 24.0f);
    lighting->clockTime = hours < 0.0f ? hours + 24.0f : hours;
    return true;
}

Value getLightingGeographicLatitude(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->geographicLatitude)};
}

bool setLightingGeographicLatitude(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 degrees = 0.0f;
    if (!takeFinite(value, degrees) || degrees < -90.0f || degrees > 90.0f)
        return false;
    lighting->geographicLatitude = degrees;
    return true;
}

Value getLightingAmbient(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{lighting->ambient};
}

bool setLightingAmbient(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* color = std::get_if<core::Color3>(&value);
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (color == nullptr || lighting == nullptr)
        return false;
    lighting->ambient = *color;
    return true;
}

Value getLightingBrightness(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->brightness)};
}

bool setLightingBrightness(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 brightness = 0.0f;
    if (!takeFinite(value, brightness) || brightness < 0.0f)
        return false;
    lighting->brightness = brightness;
    return true;
}

Value getLightingFogColor(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{lighting->fogColor};
}

bool setLightingFogColor(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* color = std::get_if<core::Color3>(&value);
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (color == nullptr || lighting == nullptr)
        return false;
    lighting->fogColor = *color;
    return true;
}

Value getLightingFogStart(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->fogStart)};
}

bool setLightingFogStart(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 distance = 0.0f;
    if (!takeFinite(value, distance) || distance < 0.0f)
        return false;
    lighting->fogStart = distance;
    return true;
}

Value getLightingFogEnd(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->fogEnd)};
}

bool setLightingFogEnd(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 distance = 0.0f;
    // Not required to exceed `fogStart`: equal or below means no fog at all,
    // which is the documented way to switch it off. Rejecting the ordering here
    // would make turning fog off an error.
    if (!takeFinite(value, distance) || distance < 0.0f)
        return false;
    lighting->fogEnd = distance;
    return true;
}

Value getLightingExposureCompensation(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    return lighting == nullptr ? Value{} : Value{static_cast<f64>(lighting->exposureCompensation)};
}

bool setLightingExposureCompensation(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::LightingComponent* lighting = writeLighting(world, id);
    if (lighting == nullptr)
        return false;
    f32 stops = 0.0f;
    // Finite, and otherwise unbounded: a scene that deliberately blows out or
    // crushes its exposure is making a picture, not a mistake.
    if (!takeFinite(value, stops))
        return false;
    lighting->exposureCompensation = stops;
    return true;
}

Value getLightingSunDirection(const scene::World& world, core::InstanceId id)
{
    const scene::LightingComponent* lighting = readLighting(world, id);
    if (lighting == nullptr)
        return Value{};
    return Value{sunDirection(lighting->clockTime, lighting->geographicLatitude)};
}

void attachLightingComponents(scene::World& world, core::InstanceId id)
{
    world.lighting().add(id, scene::LightingComponent{});
}

void detachLightingComponents(scene::World& world, core::InstanceId id)
{
    world.lighting().remove(id);
}

} // namespace luaug::render::native
