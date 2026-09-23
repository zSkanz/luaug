#include "luaug/scene/players.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <string>

namespace luaug::scene {

core::InstanceId networkServiceOf(const World& world, core::InstanceId dataModel) noexcept
{
    const ClassId networkClass = world.classes().findId(world.atoms().lookup("NetworkService"));
    if (networkClass == InvalidClass || !world.alive(dataModel))
        return {};
    return world.findFirstChildOfClass(dataModel, networkClass);
}

core::InstanceId createPlayer(World& world, core::InstanceId networkService, core::u32 userId, bool local)
{
    const ClassId playerClass = world.classes().findId(world.atoms().intern("Player"));
    if (playerClass == InvalidClass || !world.alive(networkService))
        return {};
    const core::InstanceId id = world.create(playerClass);
    if (!id.valid())
        return {};
    if (PlayerComponent* player = world.players().find(id); player != nullptr) {
        player->userId = userId;
        player->local = local;
    }
    world.setName(id, world.atoms().intern("Player" + std::to_string(userId)));
    (void)world.setParent(id, networkService);
    world.changes().push(Change{ChangeKind::InstanceEvent, networkService, id, world.atoms().intern("PlayerAdded")});
    return id;
}

void removePlayer(World& world, core::InstanceId networkService, core::InstanceId player)
{
    if (!world.alive(player))
        return;
    world.changes().push(
        Change{ChangeKind::InstanceEvent, networkService, player, world.atoms().intern("PlayerRemoving")});
    (void)world.destroy(player);
}

core::InstanceId localPlayerOf(const World& world) noexcept
{
    core::InstanceId found;
    world.players().forEach([&found](core::InstanceId id, const PlayerComponent& player) {
        if (!found.valid() && player.local)
            found = id;
    });
    return found;
}

core::InstanceId playerByUserId(const World& world, core::u32 userId) noexcept
{
    core::InstanceId found;
    world.players().forEach([&](core::InstanceId id, const PlayerComponent& player) {
        if (!found.valid() && player.userId == userId && !world.destroyed(id))
            found = id;
    });
    return found;
}

void captureLocalIntents(World& world)
{
    const core::InstanceId localId = localPlayerOf(world);
    PlayerComponent* local = localId.valid() ? world.players().find(localId) : nullptr;
    if (local == nullptr)
        return;
    local->intents.clear();
    world.inputActions().forEach([&](core::InstanceId id, const InputActionComponent& action) {
        if (!action.enabled || world.destroyed(id))
            return;
        // Only what the simulation clock resolves: a Render-rate action is a
        // camera or a menu, which is this machine's business and not a fact
        // about what the player did in the world (ADR 0039).
        const InputContextComponent* context = world.inputContexts().find(world.parentOf(id));
        if (context == nullptr || context->rate != 0 || !context->enabled)
            return;
        const core::NameAtom name = world.name(id);
        const bool seen = std::any_of(local->intents.begin(), local->intents.end(),
                                      [name](const PlayerIntent& intent) { return intent.action == name; });
        if (seen)
            return;
        local->intents.push_back(PlayerIntent{name, action.type, action.axis, action.pressed});
    });
}

} // namespace luaug::scene
