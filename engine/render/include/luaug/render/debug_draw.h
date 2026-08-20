// Immediate-mode debug geometry (roadmap M1).
//
// Deliberately early, and not a placeholder: M2 visualizes 500 scripted
// instances through this, M3 develops hot reload against it, and M5 draws
// physics wireframes with it. The real renderer arrives in M4; until then this
// is how anything gets seen at all.
//
// Immediate-mode means the caller re-submits every frame and never holds a
// handle. `clear()` at frame start, `line()`/`wireBox()` during the frame, and
// whatever was submitted is what appears -- no retained scene, no lifetime to
// get wrong, and a caller that stops drawing something makes it disappear
// without having to remember to delete it.
#pragma once

#include <span>
#include <vector>

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render
{

using core::f32;
using core::Mat4;
using core::u32;
using core::usize;
using core::Vec3;

// Packed RGBA8 rather than four floats: debug geometry is vertex-bound, and at
// 12 lines per wire box a cube's worth of colour is 96 bytes instead of 384.
struct DebugColor
{
    u32 rgba = 0xFFFFFFFFu;

    [[nodiscard]] static constexpr DebugColor fromLinear(f32 r, f32 g, f32 b, f32 a = 1.0f) noexcept
    {
        const auto channel = [](f32 value) -> u32
        {
            const f32 clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<u32>(clamped * 255.0f + 0.5f);
        };
        return {channel(r) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24)};
    }

    [[nodiscard]] constexpr bool operator==(const DebugColor&) const noexcept = default;
};

// Matches the vertex layout `shaders/src/debug_line.hlsl` declares. The
// static_assert below is the contract: the GPU reads this struct's bytes
// directly, so a padding byte here is a garbled frame there.
struct DebugVertex
{
    Vec3 position;
    DebugColor color;
};

static_assert(sizeof(DebugVertex) == 16, "DebugVertex must be tightly packed for the vertex buffer");

class DebugDraw
{
public:
    void clear() noexcept;

    void line(Vec3 from, Vec3 to, DebugColor color);

    // Axis-aligned. `halfExtents` rather than a size, because every caller of
    // this so far has a centre and a radius, and halving at each call site is
    // where the sign errors live.
    void wireBox(Vec3 center, Vec3 halfExtents, DebugColor color);

    // Twelve lines through an arbitrary transform, so an oriented box costs the
    // same as an axis-aligned one and callers do not need a second entry point.
    void wireBox(const Mat4& transform, Vec3 halfExtents, DebugColor color);

    // Three axis-aligned rings. `segments` is per ring.
    void wireSphere(Vec3 center, f32 radius, DebugColor color, u32 segments = 24);

    // Subtracts `origin` from every vertex recorded so far, turning a buffer of
    // world positions into the camera-relative space the renderer works in
    // (ADR 0014, `render_world.h`).
    //
    // It exists because the two halves of this buffer are filled at different
    // moments. `DebugService:DrawLine` records during the tick, before anything
    // has decided where the camera is; the wire boxes are appended after
    // extraction, when it is known. Converting each at its own call site means
    // one of them is always guessing, so both are recorded in world space and
    // the whole buffer is rebased once.
    //
    // **This was wrong for the whole of M4** in the other direction: the debug
    // pass drew world coordinates through the renderer's camera-relative
    // view-projection, so every wire box and every scripted line was displaced
    // by the camera's distance from the origin. The debug path is what a person
    // reaches for when the real one is not working, so it lying is worse than
    // most bugs of the same size.
    //
    // The subtraction is f32, because a `DebugVertex` is. That is the existing
    // limit of this buffer rather than one introduced here -- the M7 floating
    // origin is what changes it, and until then a debug line a few kilometres
    // from the origin loses precision the same way it always has.
    void rebaseTo(core::DVec3 origin) noexcept;

    // A short RGB triad: X red, Y green, Z blue. The fastest way to see whether
    // a transform is doing what you think.
    void axes(const Mat4& transform, f32 length);

    // Valid until the next clear() or the next call that appends.
    [[nodiscard]] std::span<const DebugVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] usize lineCount() const noexcept { return vertices_.size() / 2; }
    [[nodiscard]] bool empty() const noexcept { return vertices_.empty(); }

private:
    // One primitive type -- a line list. Everything above is composed from it,
    // which is why there is one pipeline, one buffer and one draw call. Solid
    // shapes arrive with the milestone that needs them rather than as an
    // unexercised second path.
    std::vector<DebugVertex> vertices_;
};

} // namespace luaug::render
