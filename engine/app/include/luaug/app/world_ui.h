// World-space UI (F3): `BillboardGui` and `SurfaceGui`, placed in the world.
//
// **The UI module lays each tree out and draws it into a canvas; this places
// the canvas.** A canvas is pixels, y down, like the screen's; a placement is
// the world point its top-left corner sits at and the two world vectors one
// canvas pixel steps along. Everything the screen's UI can draw -- panels,
// rounded corners, text, rich text, images -- then lands in the world through
// one affine map, and the renderer draws it in the world pass, hidden by what
// is in front of it (`shaders/src/ui_world.hlsl`).
//
// The placements are pure functions of numbers, in the tradition of the brush
// overlay and the chunk overlay, because a sign that faces the wrong way is a
// bug that reproduces by looking and should be fixed once.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/types.h"
#include "luaug/ui/ui.h"

#include <optional>
#include <span>

namespace luaug::scene {
class World;
struct BillboardGuiComponent;
struct SurfaceGuiComponent;
} // namespace luaug::scene

namespace luaug::app {

// Where a canvas sits: its top-left corner (camera-relative metres), the world
// step of one canvas pixel along x and along y, and the canvas's size in
// pixels. `distance` is from the camera to its centre, for the sort.
struct CanvasPlacement
{
    core::Vec3 topLeft;
    core::Vec3 right;
    core::Vec3 down;
    core::Vec2 canvas;
    core::f32 distance = 0.0f;
};

// A billboard's canvas pixels per metre of `Size.Scale`: what a child's offset
// and a `TextSize` are measured in when the billboard is sized in the world.
inline constexpr core::f32 BillboardPixelsPerMetre = 50.0f;

// A billboard over `anchor` (world metres, the adornee's centre plus its
// offset), facing `camera`. Nothing when it has no size, is behind the camera
// plane, or is past `MaxDistance`.
[[nodiscard]] std::optional<CanvasPlacement> placeBillboard(const scene::BillboardGuiComponent& gui, core::DVec3 anchor,
                                                            const render::RenderCamera& camera, core::Vec2 viewport);

// A surface on one face of a part, lifted a millimetre off it so it never
// fights the part for the same depth.
[[nodiscard]] std::optional<CanvasPlacement> placeSurface(const scene::SurfaceGuiComponent& gui,
                                                          const core::CFrameD& part, core::Vec3 size,
                                                          core::DVec3 cameraOrigin);

// Lays out, draws and places every enabled `BillboardGui` and `SurfaceGui`
// under `workspace` or `uiService`, and appends the result to `out`'s world UI
// geometry, back to front. `textures` is the screen UI's texture table --
// the glyph atlas and the images -- which world UI shares.
void buildWorldUi(scene::World& world, core::InstanceId workspace, core::InstanceId uiService, core::Vec2 viewport,
                  std::span<const rhi::TextureHandle> textures, ui::DrawList& scratch, render::RenderWorld& out);

} // namespace luaug::app
