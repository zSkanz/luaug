// `RemoteEvent` (N2, ADR 0077): a game's messages between machines, as the
// script module sees them.
//
// **This module owns the payload and nothing else does.** It is the only one
// that can read a Luau value, so it encodes a call's arguments into bytes and
// decodes them into arguments again. The queue in `scene::EngineState`, the
// replication engine and the wire all carry those bytes without parsing them;
// the instances the arguments name ride beside them, for the replication
// engine to translate between this machine's ids and the network's.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"
#include "luaug/script/instance_binding.h"

#include <span>
#include <string_view>
#include <vector>

struct lua_State;

namespace luaug::script {

// The largest payload one message may carry, and how deep its tables may nest.
inline constexpr core::usize MaxRemotePayload = 64u * 1024u;
inline constexpr int MaxRemoteDepth = 8;
// How many values one call may carry: the count is a byte on the wire.
inline constexpr int MaxRemoteArguments = 255;

// Encodes the `count` values from stack index `first`. **Refuses what cannot
// travel with a keyed error at the call**, so the mistake is reported where it
// was made: a function, a thread, a userdata that is not an instance, a table
// nested past `MaxRemoteDepth` or containing itself, or a payload past
// `MaxRemotePayload`.
void encodeRemoteArguments(lua_State* L, int first, int count, std::vector<core::u8>& payload,
                           std::vector<core::InstanceId>& refs);

// Pushes the values a payload holds and answers how many, or -1 -- with nothing
// pushed -- when the payload is malformed: it came off the network, and a
// message a peer mangled is dropped rather than half-delivered.
[[nodiscard]] int decodeRemoteArguments(lua_State* L, std::span<const core::u8> payload,
                                        std::span<const core::InstanceId> refs);

// Fires every message waiting in the world's inbox: `ServerReceived` with the
// sender first, or `ClientReceived`. Called once a tick, at the start, beside
// the input events.
void fireRemoteMessages(lua_State* L);

// **`RemoteFunction.OnServerInvoke`** (ADR 0079), the one callback: a member
// a script assigns a function to. Answers false when `key` is not a callback of
// `id`'s class, so the caller goes on to its other lookups. `Get` pushes the
// function or nil; `Set` stores the value at `valueIndex`, refusing anything
// that is neither a function nor nil.
[[nodiscard]] bool remoteCallbackGet(lua_State* L, core::InstanceId id, std::string_view key);
[[nodiscard]] bool remoteCallbackSet(lua_State* L, core::InstanceId id, std::string_view key, int valueIndex);

// `FireServer`, `FireClient`, `FireAllClients` and `InvokeServerAsync`, for
// the service binding table.
[[nodiscard]] std::span<const InstanceMethodBinding> remoteMethodBindings() noexcept;

} // namespace luaug::script
