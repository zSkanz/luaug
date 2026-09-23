// Players, as the world holds them (N1, ADR 0069).
//
// **A `Player` is somebody taking part, and it lives under `NetworkService`.**
// Solo has one, a host has one plus one per replica, a dedicated server has one
// per replica only, and a replica has its own. A game reads all of them through
// the same two calls -- `NetworkService:GetPlayers()` and
// `player:GetIntent(action)` -- whether or not anybody is networked, which is
// what makes a multiplayer game's code the same code as its solo one.
//
// These helpers are the one place players are made and unmade, so
// `PlayerAdded` and `PlayerRemoving` fire on every path that does either.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"

namespace luaug::scene {

class World;

// The world's `NetworkService`, found under the data model. Invalid when this
// world has none -- a stamp stage, a test world.
[[nodiscard]] core::InstanceId networkServiceOf(const World& world, core::InstanceId dataModel) noexcept;

// Creates a player under `networkService`, names it, and fires `PlayerAdded`.
core::InstanceId createPlayer(World& world, core::InstanceId networkService, core::u32 userId, bool local);

// Fires `PlayerRemoving`, then destroys the player.
void removePlayer(World& world, core::InstanceId networkService, core::InstanceId player);

// The player at this machine, or invalid.
[[nodiscard]] core::InstanceId localPlayerOf(const World& world) noexcept;

// The player with this number, or invalid.
[[nodiscard]] core::InstanceId playerByUserId(const World& world, core::u32 userId) noexcept;

// **Copies this machine's input into its own player's intents**, once a tick,
// right after the simulation-rate input dispatch -- so a script that reads its
// own player's intent reads this tick's input, and a replica sends this tick's.
//
// Every enabled action in a `Simulation` context, in pool order. A second
// action with a name already copied is skipped: an intent is looked up by name,
// and two answers to one name is a question the sender cannot mean.
void captureLocalIntents(World& world);

} // namespace luaug::scene
