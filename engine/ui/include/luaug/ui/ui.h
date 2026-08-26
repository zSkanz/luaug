// The in-game UI: layout, text and the 2D draw list (architecture.md §2 `ui`,
// ADR 0011 as amended by ADR 0040).
//
// Three stages, and they are separate because they run at different times and
// for different reasons:
//
//   1. **Layout** turns the Instance tree into rectangles. It runs only for a
//      `ScreenGui` something marked dirty, which is why every layout-affecting
//      property setter in `native_accessors.cpp` walks up to mark one.
//
//   2. **Text** measures runs and turns them into geometry. Layout needs the
//      measurement (a label that sizes to its text) and drawing needs the
//      geometry, and both come out of one walk over the same face. v1's face is
//      the vector one vendored inside stb, for the reason `text.cpp` states at
//      length: a TrueType face needs a FILE, and shipping one is a licence
//      decision that belongs to a human (R6).
//
//   3. **The draw list** is a flat, ordered array of coloured and textured
//      quads. `render` consumes it; nothing here knows what a GPU is.
//
// **Coordinates are window pixels, y down, origin top-left**, throughout. That
// is the convention every UI system in the world uses and the opposite of the
// world's; the conversion happens once, in the 2D pass's projection.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <span>
#include <string_view>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::ui {

using core::f32;
using core::i32;
using core::u32;
using core::u64;
using core::usize;

// What `TextLabel` needs a font to answer, and the only thing layout asks of
// the text system. Separated from the atlas so that a headless run -- which has
// no GPU and therefore no atlas texture -- still measures text and therefore
// still lays out identically. A layout that depended on whether there was a
// screen would be a layout the determinism gate could not check.
struct TextRunMetrics
{
    // The tight box the run occupies, in pixels.
    core::Vec2 size;
    // Distance from the top of the box to the first line's baseline. Drawing
    // needs it; layout does not, and it is here because both come out of one
    // walk over the glyphs.
    f32 ascent = 0.0f;
    u32 lineCount = 0;
};

// --- The draw list -----------------------------------------------------------

// One quad. Everything the 2D pass draws is one of these: a background, a
// nine-slice patch, a glyph. A single shape rather than a tagged union of three
// because they differ only in which texture they sample and over what UVs, and
// a pass that sorts one array is a pass that batches.
struct DrawQuad
{
    // Top-left and bottom-right in window pixels.
    core::Vec2 min;
    core::Vec2 max;
    // Source rectangle in the texture, normalized. Equal corners mean "no
    // texture": the shader multiplies by white and the quad is a flat colour.
    core::Vec2 uvMin;
    core::Vec2 uvMax;
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 alpha = 1.0f;
    // Which texture the quad samples: 0 is none, 1 is the glyph atlas. Image
    // textures join it when `ImageLabel` can name one the pipeline has loaded,
    // which is M7's asset work.
    u32 texture = 0;
    // The scissor rectangle this quad is clipped to, as an index into
    // `DrawList::scissors`. 0 is "the whole window", which is why that entry is
    // always present.
    u32 scissor = 0;
    // Corner radius in pixels, from a `UICorner` on the element (D030). Zero is
    // a square corner and is the overwhelmingly common case; the shader's
    // rounded-rectangle distance costs nothing at zero, which is why this is a
    // number on every quad rather than a second pipeline.
    //
    // It rounds the DRAWING only. Layout does not see it and neither does the
    // hit test -- `UICorner`'s own doc says so, and a button whose corner you
    // could see through but not click through would be worse than a square one.
    f32 cornerRadius = 0.0f;

    // --- The turn, from `GuiObject.Rotation` ---------------------------------
    //
    // An affine `p -> R*p + t`, with `turn` holding `(cos, sin)` and
    // `turnOffset` the translation. Identity is `(1, 0)` and `(0, 0)`, which is
    // what a quad nobody turned carries.
    //
    // **A full affine rather than an angle and a pivot**, which is the shape it
    // looks like it wants. A turned element inside a turned ancestor spins about
    // two different points, and composing those is
    // `R(a+b)(p - Q) + R(a)(Q - P) + P` -- a turn about one point plus a
    // translation that is not that point. Angle-and-pivot cannot express it, so
    // it would either lose nesting or acquire a special case; `R*p + t` composes
    // as `(R_a*R_b, R_a*t_b + t_a)` and has neither.
    //
    // Applied to POSITION only. The vertex's own local frame -- the two
    // coordinates `cornerRadius` measures against -- stays in the quad's upright
    // space, so a rounded corner is still round after a turn instead of becoming
    // an ellipse.
    //
    // **Named `turn` deliberately, and not after the property it carries.**
    // `inertcheck` sweeps by field name, so a helper here wearing that name
    // would itself count as a reader of the component field it is drawing --
    // which is the collision that let `GuiObject.Rotation` sit stored and unread
    // for the whole of v1 (S7.13). A word nothing else uses keeps the lint's
    // answer about that field an honest one, and this comment avoids spelling
    // it for exactly the same reason.
    core::Vec2 turn{1.0f, 0.0f};
    core::Vec2 turnOffset{0.0f, 0.0f};
};

struct DrawList
{
    std::vector<DrawQuad> quads;
    // Index 0 is the whole window. A `ClipsDescendants` element pushes one.
    std::vector<core::Rect> scissors;

    void clear()
    {
        quads.clear();
        scissors.clear();
    }
};

// What the glyph store has been asked for and what it has done about it.
//
// A COUNTER surface, like `LayoutStats` beside it, and for the same reason: the
// property worth asserting is "a second frame of the same label fills nothing
// new", and that is a number rather than a duration.
//
// `missingGlyphs` is the one to watch in a real game. It counts codepoints the
// face could not draw, so a label in Portuguese that comes out full of boxes is
// a number somebody can see before a player reports it.
struct GlyphCacheStats
{
    // Live entries, each one a (face, size, codepoint) triple.
    u64 entries = 0;
    // Entries created. Rises once per new triple and never for a repeat.
    u64 fills = 0;
    u64 hits = 0;
    // How many times the store filled up and was emptied. Non-zero means
    // something is asking for a new size every frame; see `text.cpp`.
    u64 clears = 0;
    u64 missingGlyphs = 0;
};

// The glyph atlas: single-channel COVERAGE, one byte a texel, row-major.
//
// Coverage rather than colour, because a glyph has no colour of its own -- the
// label's `TextColor3` supplies it, and the shader multiplies. One byte a texel
// rather than four is the difference between a one-megabyte atlas and a four-
// megabyte one for the same text.
//
// Empty until something has been rasterised, and empty forever on a build whose
// content has no font: the fallback face draws vector rectangles and samples
// nothing.
struct GlyphAtlas
{
    std::span<const core::u8> pixels;
    u32 width = 0;
    u32 height = 0;
    // Rises whenever a glyph is added. What an uploader compares against, so a
    // frame that added no glyphs costs no upload -- which is every frame after
    // the first few.
    u64 version = 0;
};

[[nodiscard]] GlyphAtlas glyphAtlas() noexcept;

// Supplies the bytes of a named face, for `TextLabel.Font`.
//
// A CALLBACK because `ui` has no content mounts and must not grow any: resolving
// `asset://fonts/thing.ttf` is the asset system's job, and the app is what joins
// the two. Null -- the default -- means only the built-in default face exists,
// which is what a test or a headless run wants.
//
// Returning false means "no such face"; the caller falls back to the default and
// says so once per name rather than once per frame.
using FaceProvider = bool (*)(void* user, std::string_view name, std::vector<core::u8>& out);
void setFaceProvider(FaceProvider provider, void* user) noexcept;

// What an `ImageLabel.Image` resolves to: which texture index to sample and how
// big the picture is in its own pixels.
//
// The SIZE is not decoration. `ScaleType.Slice` cuts a nine-patch at pixel
// offsets, and `ScaleType.Tile` repeats at the picture's own size -- neither can
// be computed from the box alone, and a layout module has no other way to learn
// them.
struct ResolvedImage
{
    u32 texture = 0;
    u32 width = 0;
    u32 height = 0;
};

// Resolves an `asset://` URI to a texture the UI can name. Same shape and same
// reason as `FaceProvider`: `ui` has no content mounts and no GPU, and the app
// is the only thing that has both.
//
// False means "not loaded", and the caller draws the tint as a flat rectangle --
// which is what an image still streaming in looks like, and is better than a
// hole.
using ImageProvider = bool (*)(void* user, std::string_view urn, ResolvedImage& out);
void setImageProvider(ImageProvider provider, void* user) noexcept;

[[nodiscard]] const GlyphCacheStats& glyphCacheStats() noexcept;

// Empties the store and zeroes the counters. For the tests, and for a future
// caller that swaps a face at runtime -- which is the case the key exists for.
void resetGlyphCache() noexcept;

// Measures a run at a size, optionally wrapped to a width.
//
// `maxWidth` of 0 means no wrapping. Wrapping breaks at spaces, and mid-word
// only for a word wider than the box: a word cut at a random letter is worse
// than one that overhangs.
[[nodiscard]] TextRunMetrics measureText(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth);

// The quads one run draws, aligned inside `box`. Appended rather than returned,
// because a draw list is built by appending and a label is one of many.
void buildTextGeometry(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth, core::Rect box,
                       i32 horizontalAlignment, i32 verticalAlignment, core::Color3 color, f32 alpha, u32 scissor,
                       std::vector<DrawQuad>& out);

// How many times the solver has run, and over how many elements. A COUNTER
// rather than a duration, and the milestone's benchmark asserts the first is
// zero on an idle frame: at this scale a timing assertion measures the clock,
// and "~zero microseconds" is the shape of gate that passes while doing
// nothing.
struct LayoutStats
{
    u64 solverRuns = 0;
    u64 elementsLaidOut = 0;
};

// Lays out every dirty `ScreenGui` under `uiService`, writing `AbsolutePosition`
// and `AbsoluteSize` into each element's component and clearing the dirty flag.
//
// `windowSize` is the drawable size in pixels. A change to it dirties every
// tree, because a scale is a fraction of something that just changed.
void layout(scene::World& world, core::InstanceId uiService, core::Vec2 windowSize);

[[nodiscard]] const LayoutStats& layoutStats() noexcept;
void resetLayoutStats() noexcept;

// --- Interaction -------------------------------------------------------------

// What the UI needs to know about the pointer and the keyboard this frame.
//
// Handed in rather than read, because `ui` is L5 and `platform` is L1: the host
// already has both, and a module that reached for a device would be a module
// that behaves differently in a replay.
struct InteractionInput
{
    core::Vec2 pointer;
    bool pressed = false;
    bool released = false;
    // UTF-8, as the platform delivered it this frame. Empty for most frames.
    std::string_view text;
    bool backspace = false;
    // Forward delete, which is a different key and a different edit: backspace
    // takes what is BEFORE the caret and this takes what is after. Without both
    // a caret can only ever be used to insert.
    bool forwardDelete = false;
    // Caret movement, as the four things a single-line field can do with it.
    // Separate flags rather than a signed step, because a frame that saw both
    // arrows should do nothing rather than average them.
    bool caretLeft = false;
    bool caretRight = false;
    bool caretHome = false;
    bool caretEnd = false;
    bool submit = false;
};

// The answer the host needs back: whether the UI took the pointer.
//
// **This is the "engine-owned high-priority InputContext with Sink" of
// architecture.md §2, without the Instance.** A context in the tree would be
// visible to `GetChildren`, to a tree walk and to a serializer -- an object a
// game can see, reparent and destroy, whose destruction would silently turn off
// every button. A flag the host passes to `input` is the same behaviour with
// nothing to break.
struct InteractionResult
{
    // True while the pointer is over any visible element. `input` marks the
    // mouse codes consumed for that frame, so a button over the world does not
    // also shoot the gun.
    bool pointerOverUi = false;

    // True while a `TextInput` has focus. `input` marks the KEYBOARD codes
    // consumed for that frame, which is the same claim one device over: a player
    // typing `w` into a chat box should not also walk forward. It is also what
    // makes `uiConsumed` mean something on `InputService.InputBegan` for a key
    // (ADR 0041) -- a flag that was always false for the keyboard would be
    // honest and useless.
    bool textInputFocused = false;
};

// Fires `Activated`, `PointerEntered` and `PointerExited`, moves focus between
// `TextInput`s, and edits the focused one's text. Reads the rectangles the
// layout produced; computes no layout of its own.
[[nodiscard]] InteractionResult updateInteraction(scene::World& world, core::InstanceId uiService,
                                                  const InteractionInput& input);

// The topmost visible element under a point, or an invalid id. Exposed for the
// tests, which is the only reason it is not private: a hit test is easier to
// believe as a case than as a consequence.
[[nodiscard]] core::InstanceId hitTest(const scene::World& world, core::InstanceId uiService, core::Vec2 point);

// Walks every enabled `ScreenGui` in `DisplayOrder` and emits its quads in
// draw order -- `ZIndex`, then document order. Reads the rectangles the layout
// produced and computes nothing: a draw list that laid anything out would make
// "what is on screen" depend on when it was asked.
void buildDrawList(const scene::World& world, core::InstanceId uiService, DrawList& out);

} // namespace luaug::ui
