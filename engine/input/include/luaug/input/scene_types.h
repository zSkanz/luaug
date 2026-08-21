// The classes `input` owns, registered into scene's registry
// (architecture.md §2, rule 3).
//
// The same arrangement `render/scene_types.h` describes: `scene` (L3) may not
// include `input` (L4), so these descriptors are handed down rather than
// declared in scene's own table. Everything behind this call is generated from
// `api/defs/*.api.luau`.
#pragma once

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"

namespace luaug::input {

// InputContext, InputAction, InputBinding and InputService.
//
// MUST run after `scene::generated::registerClasses`, for the reason render's
// does: every one of them extends `Instance`, and a subclass is registered by
// naming its parent's `ClassId`. `app` orders the calls
// (engine/app/src/world_host.cpp).
void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms);

} // namespace luaug::input
