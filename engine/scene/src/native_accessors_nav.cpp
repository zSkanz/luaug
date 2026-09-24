// `NavigationService`'s agent properties (ADR 0089). Each write refuses what
// no agent can be, here, where a refusal is a keyed error -- rather than in
// the mesh builder, where it would be a mesh of nothing.
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
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

// One getter and one setter per field, with the setter's own rule.
template <f32 NavigationComponent::*Field>
[[nodiscard]] Value getField(const World& world, core::InstanceId id)
{
    const NavigationComponent* c = world.navigation().find(id);
    return c == nullptr ? Value{} : Value{static_cast<f64>(c->*Field)};
}

template <f32 NavigationComponent::*Field, class Rule>
[[nodiscard]] bool setField(World& world, core::InstanceId id, const Value& value, Rule rule)
{
    NavigationComponent* c = world.navigation().find(id);
    f32 next = 0.0f;
    if (c == nullptr || !takeF32(value, next) || !rule(next))
        return false;
    c->*Field = next;
    return true;
}

} // namespace

void attachNavigationComponents(World& world, core::InstanceId id)
{
    world.navigation().add(id, NavigationComponent{});
}

void detachNavigationComponents(World& world, core::InstanceId id)
{
    world.navigation().remove(id);
}

Value getNavigationServiceAgentRadius(const World& world, core::InstanceId id)
{
    return getField<&NavigationComponent::agentRadius>(world, id);
}

bool setNavigationServiceAgentRadius(World& world, core::InstanceId id, const Value& value)
{
    return setField<&NavigationComponent::agentRadius>(world, id, value, [](f32 v) { return v > 0.0f; });
}

Value getNavigationServiceAgentHeight(const World& world, core::InstanceId id)
{
    return getField<&NavigationComponent::agentHeight>(world, id);
}

bool setNavigationServiceAgentHeight(World& world, core::InstanceId id, const Value& value)
{
    return setField<&NavigationComponent::agentHeight>(world, id, value, [](f32 v) { return v > 0.0f; });
}

Value getNavigationServiceAgentMaxClimb(const World& world, core::InstanceId id)
{
    return getField<&NavigationComponent::agentMaxClimb>(world, id);
}

bool setNavigationServiceAgentMaxClimb(World& world, core::InstanceId id, const Value& value)
{
    return setField<&NavigationComponent::agentMaxClimb>(world, id, value, [](f32 v) { return v >= 0.0f; });
}

Value getNavigationServiceAgentMaxSlope(const World& world, core::InstanceId id)
{
    return getField<&NavigationComponent::agentMaxSlope>(world, id);
}

bool setNavigationServiceAgentMaxSlope(World& world, core::InstanceId id, const Value& value)
{
    return setField<&NavigationComponent::agentMaxSlope>(world, id, value,
                                                         [](f32 v) { return v >= 0.0f && v <= 89.0f; });
}

} // namespace luaug::scene::native
