// Drawing the streaming grid in the world (E5's last gate row).
//
// **The Stats panel already prints the chunk states and that is not the same
// thing.** A grid of letters tells you eleven cells are resident; it does not
// tell you WHERE the seam is, which cell the character is standing in, or that
// the ring is lopsided because a focus is off by half a cell. Streaming is a
// question about space, and the answer belongs in the space.
//
// It is also the only form of the answer a gate can capture. E5 owes a
// screenshot of the chunk state and has owed it since the milestone closed,
// because the ImGui shell cannot render headlessly -- and a picture drawn
// through `DebugDraw` goes through the ordinary renderer, which
// `--headless --screenshot` already captures. So the version that is better for
// a person is also the version that can be gated, which is why this exists
// rather than a way to screenshot a panel.
//
// Behind a switch, like the physics wireframe: `DebugService:ShowPanel`
// ("Streaming") in a game, `View > Streaming Grid` in the editor.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render {
class DebugDraw;
}

namespace luaug::app {

class StreamingHost;

// How high above `y` the cell outlines are drawn, and how tall the corner posts
// are. A chunk is vertically unbounded (`chunkBounds` says so and means it), so
// a box around one would be infinitely tall; what is drawn instead is the
// footprint at a height a person is looking at, with short posts so a cell reads
// as a cell rather than as a line on the ground.
inline constexpr core::f32 kChunkPostMetres = 2.0f;

// Appends the grid, in WORLD space -- which is what every other debug line
// records, because `DebugDraw` rebases the whole buffer once after extraction
// (D011).
//
// `groundY` is where the footprints are drawn. The caller passes the focus's own
// height, so the grid follows a character up a hill instead of being buried in
// it.
//
// Appends nothing for a host that is not streaming.
void drawChunkGrid(const StreamingHost& streaming, core::f64 groundY, render::DebugDraw& draw);

} // namespace luaug::app
