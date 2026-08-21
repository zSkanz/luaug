// The classes `audio` owns, registered into scene's registry
// (architecture.md §2, rule 3).
#pragma once

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"

namespace luaug::audio {

// `Sound`, `AudioGroup` and `AudioService`.
//
// MUST run after `scene::generated::registerClasses`, like every other module's:
// all three extend `Instance`, and a subclass is registered by naming its
// parent's `ClassId`.
void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms);

} // namespace luaug::audio
