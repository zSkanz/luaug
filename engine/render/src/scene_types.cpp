#include "luaug/render/scene_types.h"

// By a path relative to this file rather than through an include directory.
// Every module's generated header has the same file name, so putting two of
// them on one include path would make `#include "class_descriptors.gen.h"`
// resolve by search order -- which is how a translation unit ends up quietly
// registering the wrong module's classes. `engine/scene/src/native_accessors.cpp`
// reaches its own the same way.
#include "../generated/class_descriptors.gen.h"

namespace luaug::render {

void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms)
{
    generated::registerClasses(classes, atoms);
}

} // namespace luaug::render
