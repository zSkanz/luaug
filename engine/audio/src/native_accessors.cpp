// The hand-written half of `audio`'s reflection (architecture.md §4).
//
// Eleven properties, and three of them are worth reading. `Playing` is a
// property AND a pair of methods, so the write path is the same code the methods
// call. `TimePosition` is writable, which is how a script seeks. `Group` is an
// Instance reference and therefore the one property here that can be nil.
#include "luaug/audio/scene_types.h"
#include "luaug/scene/world.h"

#include <cmath>

#include "../generated/class_descriptors.gen.h"

namespace luaug::audio {
namespace native {
namespace {

using core::f32;
using core::f64;
using scene::Value;

[[nodiscard]] bool isFinite(f64 value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] scene::SoundComponent* sound(scene::World& world, core::InstanceId id)
{
    return world.sounds().find(id);
}

[[nodiscard]] const scene::SoundComponent* sound(const scene::World& world, core::InstanceId id)
{
    return world.sounds().find(id);
}

// A non-negative number, which is what almost every property here is.
[[nodiscard]] bool takeAtLeastZero(const Value& value, f32& out)
{
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number) || *number < 0.0)
        return false;
    out = static_cast<f32>(*number);
    return true;
}

} // namespace

// --- AudioGroup --------------------------------------------------------------

Value getAudioGroupVolume(const scene::World& world, core::InstanceId id)
{
    const scene::AudioGroupComponent* group = world.audioGroups().find(id);
    return group == nullptr ? Value{} : Value{static_cast<f64>(group->volume)};
}

bool setAudioGroupVolume(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::AudioGroupComponent* group = world.audioGroups().find(id);
    return group != nullptr && takeAtLeastZero(value, group->volume);
}

void attachAudioGroupComponents(scene::World& world, core::InstanceId id)
{
    world.audioGroups().add(id, scene::AudioGroupComponent{});
}

void detachAudioGroupComponents(scene::World& world, core::InstanceId id)
{
    world.audioGroups().remove(id);
}

// --- Sound -------------------------------------------------------------------

Value getSoundContent(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{self->content};
}

bool setSoundContent(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* text = std::get_if<std::string>(&value);
    scene::SoundComponent* self = sound(world, id);
    if (text == nullptr || self == nullptr)
        return false;
    self->content = *text;
    // A new sound is a new thing to load, so `Loaded` fires again. It costs
    // nothing today -- there is no file -- and it is the behaviour a caller will
    // expect the moment there is one.
    self->loadedFired = false;
    return true;
}

Value getSoundPlaying(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{self->playing};
}

bool setSoundPlaying(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::SoundComponent* self = sound(world, id);
    if (flag == nullptr || self == nullptr)
        return false;
    // Deliberately NOT a rewind. `Playing = false` is `Pause`, not `Stop`: the
    // property says whether the timeline is advancing, and rewinding on a write
    // would make `Playing = false; Playing = true` mean something different from
    // `Pause(); Play()`.
    self->playing = *flag;
    return true;
}

Value getSoundLooped(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{self->looped};
}

bool setSoundLooped(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::SoundComponent* self = sound(world, id);
    if (flag == nullptr || self == nullptr)
        return false;
    self->looped = *flag;
    return true;
}

Value getSoundVolume(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{static_cast<f64>(self->volume)};
}

bool setSoundVolume(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SoundComponent* self = sound(world, id);
    return self != nullptr && takeAtLeastZero(value, self->volume);
}

Value getSoundPlaybackSpeed(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{static_cast<f64>(self->playbackSpeed)};
}

bool setSoundPlaybackSpeed(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    scene::SoundComponent* self = sound(world, id);
    // Zero is refused rather than treated as a pause. `Playing` is what pauses,
    // and a speed of zero would be a sound that never ends -- which is a hang
    // dressed as a property.
    if (number == nullptr || self == nullptr || !isFinite(*number) || *number <= 0.0)
        return false;
    self->playbackSpeed = static_cast<f32>(*number);
    return true;
}

Value getSoundTimePosition(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{self->timePosition};
}

bool setSoundTimePosition(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    scene::SoundComponent* self = sound(world, id);
    if (number == nullptr || self == nullptr || !isFinite(*number) || *number < 0.0)
        return false;
    self->timePosition = *number;
    return true;
}

Value getSoundRollOffMinDistance(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{static_cast<f64>(self->rollOffMinDistance)};
}

bool setSoundRollOffMinDistance(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SoundComponent* self = sound(world, id);
    return self != nullptr && takeAtLeastZero(value, self->rollOffMinDistance);
}

Value getSoundRollOffMaxDistance(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    return self == nullptr ? Value{} : Value{static_cast<f64>(self->rollOffMaxDistance)};
}

bool setSoundRollOffMaxDistance(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SoundComponent* self = sound(world, id);
    return self != nullptr && takeAtLeastZero(value, self->rollOffMaxDistance);
}

Value getSoundGroup(const scene::World& world, core::InstanceId id)
{
    const scene::SoundComponent* self = sound(world, id);
    if (self == nullptr || !world.alive(self->group))
        return Value{};
    return Value{self->group};
}

bool setSoundGroup(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::SoundComponent* self = sound(world, id);
    if (self == nullptr)
        return false;

    // nil clears it, which is what an optional Instance property means.
    if (std::holds_alternative<std::monostate>(value)) {
        self->group = {};
        return true;
    }

    const auto* reference = std::get_if<core::InstanceId>(&value);
    // Refused unless it really is an `AudioGroup`. A `Part` assigned here would
    // read back as a Part and mix as nothing, which is the silent kind of wrong.
    if (reference == nullptr || world.audioGroups().find(*reference) == nullptr)
        return false;
    self->group = *reference;
    return true;
}

void attachSoundComponents(scene::World& world, core::InstanceId id)
{
    world.sounds().add(id, scene::SoundComponent{});
}

void detachSoundComponents(scene::World& world, core::InstanceId id)
{
    world.sounds().remove(id);
}

// --- AudioService ------------------------------------------------------------

Value getAudioServiceMasterVolume(const scene::World& world, core::InstanceId)
{
    return Value{static_cast<f64>(world.engineState().masterVolume)};
}

bool setAudioServiceMasterVolume(scene::World& world, core::InstanceId, const Value& value)
{
    return takeAtLeastZero(value, world.engineState().masterVolume);
}

} // namespace native

void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms)
{
    generated::registerClasses(classes, atoms);
}

} // namespace luaug::audio
