// The classes `render` owns, registered into scene's registry
// (architecture.md §2, rule 3 and §2's `render` interface).
//
// `scene` (L3) may not include `render` (L4), so a renderable class cannot be
// registered from scene's own table. Instead every higher module hands its
// descriptors down: this is the one entry point `app` calls, and everything
// behind it is generated from `api/defs/*.api.luau`.
#pragma once

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"

namespace luaug::render
{

// Camera, MeshPart, PointLight, SpotLight and Sky.
//
// MUST run after `scene::generated::registerClasses`: these classes extend
// classes `scene` owns, and a subclass is registered by naming its parent's
// `ClassId`, which only exists once the parent is in. `app` orders the two
// calls (engine/app/src/world_host.cpp).
//
// architecture.md §2 sketches this as taking the registry alone. It takes the
// `AtomTable` as well because a descriptor stores interned names, and the
// table that interns them has to be the one the world reads them back through
// -- the same pair `scene::generated::registerClasses` takes.
void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms);

} // namespace luaug::render
