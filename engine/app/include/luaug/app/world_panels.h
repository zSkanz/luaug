// The Terrain and Blocks panels' authoring sections: what a world is made at and
// made from, as opposed to the brushes that shape it.
//
// **Their own file**, beside the shell rather than inside it, because each is a
// self-contained form over one verb of `Editor` and the shell only has to say
// where it goes. Declared unconditionally and inert in a shipping build, the
// shape ADR 0011 asks for -- the caller carries no `#ifdef`.
#pragma once

#include "luaug/core/id.h"

namespace luaug::scene {
class World;
}

namespace luaug::app {

class Editor;
class Inspector;
struct EditorCommands;

// The Terrain panel's two authoring sections.
//
// - **Heightmap**: an image from the machine laid over the ground, at a size
//   and between two heights, which is how every terrain editor begins a real
//   landscape rather than sculpting one from a flat square.
// - **Settings**: `VoxelSize` and the height range, the three numbers a
//   terrain is decided at, written through the inspector so they undo like any
//   other property. `VoxelSize` is offered only while the terrain is empty,
//   because the property refuses anything else and a control that is refused
//   every time is worse than one that says why it is greyed.
void drawTerrainSetup(Editor& editor, scene::World& world, core::InstanceId root, Inspector& inspector,
                      EditorCommands& commands);

// The selected block type's images and opacity: what `SetBlockTextures` and
// `SetBlockOpacity` set from a script, set here. Draws nothing when no type is
// selected.
void drawBlockLook(Editor& editor, scene::World& world, Inspector& inspector);

} // namespace luaug::app
