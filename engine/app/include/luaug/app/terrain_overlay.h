// Drawing the terrain's own triangles and normals, for `View > Terrain
// Wireframe` and `View > Terrain Normals` and for `DebugService:ShowPanel
// ("Terrain")` in a running game (the owner's terrain report, 2026-09-23).
//
// **The rendered terrain is a picture of the mesh, and the mesh is what the
// report was about.** Black regions, faces that vanish at some angles, stretched
// triangles, seams: each is a claim about the triangles, and the shaded picture
// cannot tell a wrong normal from a dark material or a hole from a shadow.
// These lines can.
//
// - **Wireframe**: every triangle's edges, green where the triangle's winding
//   agrees with its vertices' normals and red where it does not -- so an
//   inverted face is a red triangle rather than a guess.
// - **Normals**: a short yellow line along each vertex's normal, which is what
//   the shader lights by.
//
// Meshed at full detail around the camera and cached until the ground or the
// camera's cell changes, because the renderer's own meshes live on the GPU and
// the whole point is to show the CPU's answer.
#pragma once

#include "luaug/core/math.h"

namespace luaug::render {
class DebugDraw;
}

namespace luaug::scene {
class World;
}

namespace luaug::app {

// Appends the terrain around the ground the camera at `eye` looks at along
// `forward`, in WORLD space -- or around the eye itself when the look meets no
// ground. Meshed at the level of detail its distance calls for, over a square
// that widens with it, so what is drawn is the ground in view at a density that
// can be read. Nothing when neither is asked for or
// the world has no terrain.
void drawTerrainDebug(const scene::World& world, core::DVec3 eye, core::Vec3 forward, bool wireframe, bool normals,
                      render::DebugDraw& draw);

} // namespace luaug::app
