#include "luaug/scene/world.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>

namespace luaug::ui {
namespace {

using core::Rect;
using core::Vec2;

// One element, with everything the sort needs. Flattened before sorting rather
// than sorted in the tree, because `ZIndex` is ONE ordering across the whole
// ScreenGui: a per-parent stacking context is the part of CSS nobody can hold
// in their head, and §2.2 says so.
struct Entry
{
    core::InstanceId id;
    f32 zIndex = 0.0f;
    // Document-order rank, which is what breaks a ZIndex tie. Assigned by the
    // walk, so it is a pure function of the tree (R10).
    u32 order = 0;
    u32 scissor = 0;
};

// --- Images ------------------------------------------------------------------

// Set by the app, which is the only thing that can see both a content mount and
// a GPU. Null is the ordinary state of a test and of a headless run with no
// content: every image then draws as its flat tint.
ImageProvider g_imageProvider = nullptr;
void* g_imageProviderUser = nullptr;

[[nodiscard]] bool resolveImage(std::string_view urn, ResolvedImage& out)
{
    if (g_imageProvider == nullptr) {
        return false;
    }
    return g_imageProvider(g_imageProviderUser, urn, out) && out.texture != 0 && out.width > 0 && out.height > 0;
}

// One textured quad, in box pixels and source pixels.
//
// Source pixels rather than normalised UVs at every call site, because every
// rule below -- a nine-slice cut, a tile step, a letterbox -- is stated in the
// picture's own pixels, and converting once here is what keeps them readable.
void pushImageQuad(core::Rect box, core::Rect source, const ResolvedImage& image, core::Color3 tint, u32 scissor,
                   f32 cornerRadius, std::vector<DrawQuad>& out)
{
    if (box.max.x <= box.min.x || box.max.y <= box.min.y) {
        return;
    }
    DrawQuad quad;
    quad.min = box.min;
    quad.max = box.max;
    quad.uvMin = Vec2{source.min.x / static_cast<f32>(image.width), source.min.y / static_cast<f32>(image.height)};
    quad.uvMax = Vec2{source.max.x / static_cast<f32>(image.width), source.max.y / static_cast<f32>(image.height)};
    quad.color = tint;
    quad.alpha = 1.0f;
    quad.texture = image.texture;
    quad.scissor = scissor;
    quad.cornerRadius = cornerRadius;
    out.push_back(quad);
}

// Nine-slice. The four corners keep their own size, the four edges stretch along
// one axis, and the middle stretches along both -- which is how a panel keeps
// its rounded corners at any size.
void appendSlice(core::Rect box, const ResolvedImage& image, core::Rect centre, core::Color3 tint, u32 scissor,
                 std::vector<DrawQuad>& out)
{
    const auto width = static_cast<f32>(image.width);
    const auto height = static_cast<f32>(image.height);

    // The cuts, in source pixels, clamped into the picture. An inverted or
    // out-of-range `SliceCenter` is the caller's to see: its own doc says it is
    // kept as given rather than corrected, so this clamps only enough to keep
    // the arithmetic from producing negative rectangles.
    const f32 left = std::fmin(std::fmax(centre.min.x, 0.0f), width);
    const f32 top = std::fmin(std::fmax(centre.min.y, 0.0f), height);
    const f32 right = std::fmin(std::fmax(centre.max.x, left), width);
    const f32 bottom = std::fmin(std::fmax(centre.max.y, top), height);

    // Source columns and rows.
    const f32 sx[4] = {0.0f, left, right, width};
    const f32 sy[4] = {0.0f, top, bottom, height};

    // Destination columns and rows. The corners take their source size; the
    // middle takes whatever is left, and never less than nothing -- a box
    // narrower than its own two corners collapses the middle rather than
    // drawing the corners on top of each other.
    const f32 boxWidth = box.max.x - box.min.x;
    const f32 boxHeight = box.max.y - box.min.y;
    const f32 leftEdge = std::fmin(left, boxWidth);
    const f32 rightEdge = std::fmin(width - right, std::fmax(boxWidth - leftEdge, 0.0f));
    const f32 topEdge = std::fmin(top, boxHeight);
    const f32 bottomEdge = std::fmin(height - bottom, std::fmax(boxHeight - topEdge, 0.0f));

    const f32 dx[4] = {box.min.x, box.min.x + leftEdge, box.max.x - rightEdge, box.max.x};
    const f32 dy[4] = {box.min.y, box.min.y + topEdge, box.max.y - bottomEdge, box.max.y};

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            pushImageQuad(core::Rect{{dx[column], dy[row]}, {dx[column + 1], dy[row + 1]}},
                          core::Rect{{sx[column], sy[row]}, {sx[column + 1], sy[row + 1]}}, image, tint, scissor,
                          // No rounding on a slice: the picture is what supplies
                          // the corner, which is the entire point of using one.
                          0.0f, out);
        }
    }
}

// Repeated at its own size until the box is full, clipped at the far edges.
void appendTile(core::Rect box, const ResolvedImage& image, core::Color3 tint, u32 scissor, std::vector<DrawQuad>& out)
{
    const auto width = static_cast<f32>(image.width);
    const auto height = static_cast<f32>(image.height);

    // Bounded, and the bound is a real decision: a one-pixel picture tiled over
    // a full-screen frame is nearly a million quads, and the frame that built
    // them would be the hitch. Past the cap the picture stretches instead, which
    // is visibly wrong in a way somebody can find -- unlike a frame that simply
    // stops responding.
    constexpr int MaxTiles = 4096;
    const f32 columns = std::ceil((box.max.x - box.min.x) / std::fmax(width, 1.0f));
    const f32 rows = std::ceil((box.max.y - box.min.y) / std::fmax(height, 1.0f));
    if (columns * rows > static_cast<f32>(MaxTiles)) {
        pushImageQuad(box, core::Rect{{0.0f, 0.0f}, {width, height}}, image, tint, scissor, 0.0f, out);
        return;
    }

    for (f32 y = box.min.y; y < box.max.y; y += height) {
        for (f32 x = box.min.x; x < box.max.x; x += width) {
            // The last tile in a row or column is CUT rather than overhanging:
            // the source rectangle shrinks with the destination, so the picture
            // is clipped at its own resolution instead of being squashed.
            const f32 visibleWidth = std::fmin(width, box.max.x - x);
            const f32 visibleHeight = std::fmin(height, box.max.y - y);
            pushImageQuad(core::Rect{{x, y}, {x + visibleWidth, y + visibleHeight}},
                          core::Rect{{0.0f, 0.0f}, {visibleWidth, visibleHeight}}, image, tint, scissor, 0.0f, out);
        }
    }
}

void appendImageQuads(core::Rect box, const ResolvedImage& image, bool ready, i32 scaleType, core::Rect sliceCenter,
                      core::Color3 tint, u32 scissor, f32 cornerRadius, std::vector<DrawQuad>& out)
{
    if (!ready) {
        // The flat tint, which is what M6 drew for every image and is now what a
        // picture looks like while it is still arriving.
        DrawQuad quad;
        quad.min = box.min;
        quad.max = box.max;
        quad.color = tint;
        quad.alpha = 1.0f;
        quad.scissor = scissor;
        quad.cornerRadius = cornerRadius;
        out.push_back(quad);
        return;
    }

    switch (scaleType) {
    case 1:
        appendSlice(box, image, sliceCenter, tint, scissor, out);
        return;
    case 2:
        appendTile(box, image, tint, scissor, out);
        return;
    default:
        // Stretch: the whole picture into the whole box, aspect ratio and all.
        pushImageQuad(box, core::Rect{{0.0f, 0.0f}, {static_cast<f32>(image.width), static_cast<f32>(image.height)}},
                      image, tint, scissor, cornerRadius, out);
        return;
    }
}

// --- Scroll bars -------------------------------------------------------------

// How far along an axis the canvas can move, and how much of it is showing.
struct ScrollAxis
{
    f32 canvas = 0.0f;
    f32 view = 0.0f;
    f32 offset = 0.0f;

    [[nodiscard]] bool scrollable() const noexcept { return canvas > view && view > 0.0f; }
};

// The bar for one axis, appended to `out`.
//
// **Drawn with the PARENT's scissor rather than the region's own**, which is the
// one thing about a scroll bar that is easy to get wrong: a `ScrollFrame` clips
// its descendants, and a bar clipped by that clip would be scrolled away by the
// content it is reporting on.
void appendScrollBar(core::Rect box, ScrollAxis axis, f32 thickness, bool vertical, core::Color3 color, u32 scissor,
                     std::vector<DrawQuad>& out)
{
    if (!axis.scrollable() || thickness <= 0.0f) {
        return;
    }

    // The track. Along the far edge, inset by nothing: a bar that floated inside
    // its region would overlap the content it is next to.
    const core::Rect track = vertical ? core::Rect{{box.max.x - thickness, box.min.y}, {box.max.x, box.max.y}}
                                      : core::Rect{{box.min.x, box.max.y - thickness}, {box.max.x, box.max.y}};

    DrawQuad trackQuad;
    trackQuad.min = track.min;
    trackQuad.max = track.max;
    trackQuad.color = color;
    // A quarter, so the track reads as a groove rather than as a second panel.
    // Both parts take their colour from the frame's own `BackgroundColor` --
    // v1 has no theme, and a hard-coded grey would be wrong on half of them.
    trackQuad.alpha = 0.25f;
    trackQuad.scissor = scissor;
    out.push_back(trackQuad);

    // The thumb: as long a fraction of the track as the view is of the canvas,
    // and never shorter than the bar is wide -- a thumb of two pixels in a very
    // long canvas is a thumb nobody can grab.
    const f32 trackLength = vertical ? (track.max.y - track.min.y) : (track.max.x - track.min.x);
    const f32 minimum = std::fmin(thickness * 2.0f, trackLength);
    const f32 length = std::fmax(minimum, trackLength * (axis.view / axis.canvas));
    const f32 room = std::fmax(0.0f, axis.canvas - axis.view);
    const f32 travel = trackLength - length;
    const f32 start = room > 0.0f ? travel * (axis.offset / room) : 0.0f;

    DrawQuad thumb;
    thumb.min = vertical ? core::Vec2{track.min.x, track.min.y + start} : core::Vec2{track.min.x + start, track.min.y};
    thumb.max = vertical ? core::Vec2{track.max.x, track.min.y + start + length}
                         : core::Vec2{track.min.x + start + length, track.max.y};
    thumb.color = color;
    thumb.alpha = 0.75f;
    thumb.scissor = scissor;
    // Rounded to half its thickness, which is a capsule. The one piece of
    // styling here, and it costs nothing: `UICorner`'s distance field is on
    // every quad already (D030).
    thumb.cornerRadius = thickness * 0.5f;
    out.push_back(thumb);
}

void collect(const scene::World& world, core::InstanceId id, u32 scissor, std::vector<Entry>& entries,
             std::vector<Rect>& scissors)
{
    const scene::UIObjectComponent* self = world.uiObjects().find(id);
    if (self == nullptr || !self->visible)
        return;

    entries.push_back(Entry{id, self->zIndex, static_cast<u32>(entries.size()), scissor});

    u32 childScissor = scissor;
    // A ScrollFrame clips whatever `ClipsDescendants` says: a scrolling region
    // that did not clip would not be one.
    const bool clips = self->clipsDescendants || world.scrollFrames().find(id) != nullptr;
    if (clips) {
        const Rect own{self->absolutePosition, self->absolutePosition + self->absoluteSize};
        const Rect& outer = scissors[scissor];
        // Intersected with what is already in force, so a clip inside a clip
        // narrows rather than widening: a child that escaped its grandparent's
        // clip is the classic scrolling-list defect.
        scissors.push_back(Rect{Vec2{std::fmax(own.min.x, outer.min.x), std::fmax(own.min.y, outer.min.y)},
                                Vec2{std::fmin(own.max.x, outer.max.x), std::fmin(own.max.y, outer.max.y)}});
        childScissor = static_cast<u32>(scissors.size() - 1);
    }

    for (core::InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child))
        collect(world, child, childScissor, entries, scissors);
}

void emit(const scene::World& world, const Entry& entry, DrawList& out)
{
    const scene::UIObjectComponent* self = world.uiObjects().find(entry.id);
    if (self == nullptr)
        return;

    const Rect box{self->absolutePosition, self->absolutePosition + self->absoluteSize};
    const f32 backgroundAlpha = 1.0f - self->backgroundTransparency;

    // A fully transparent background still lays out and still hit-tests; it
    // just does not draw. Skipping the quad rather than submitting an invisible
    // one is the difference between a HUD that costs nothing and one that costs
    // a draw per element.
    // `UICorner`, if the element has one (D030). Clamped to half the shorter
    // side, because a radius past that is a circle and anything beyond it is
    // arithmetic with no meaning -- and clamping here rather than in the shader
    // keeps the fragment stage a distance function with no special cases.
    f32 cornerRadius = 0.0f;
    for (core::InstanceId child = world.firstChild(entry.id); child.valid(); child = world.nextSibling(child)) {
        if (const scene::UICornerComponent* corner = world.uiCorners().find(child); corner != nullptr) {
            const f32 width = box.max.x - box.min.x;
            const f32 height = box.max.y - box.min.y;
            const f32 shorter = std::fmin(width, height);
            // A `UDim`, so `Scale` is a fraction of the SHORTER side: a radius
            // that meant a fraction of the width would make a wide button's
            // corners taller than its height.
            const f32 radius = corner->cornerRadius.scale * shorter + corner->cornerRadius.offset;
            cornerRadius = std::fmax(0.0f, std::fmin(radius, shorter * 0.5f));
            break;
        }
    }

    if (backgroundAlpha > 0.0f) {
        DrawQuad quad;
        quad.min = box.min;
        quad.max = box.max;
        quad.color = self->backgroundColor;
        quad.alpha = backgroundAlpha;
        quad.scissor = entry.scissor;
        quad.cornerRadius = cornerRadius;
        out.quads.push_back(quad);
    }

    if (const scene::ScrollFrameComponent* scroll = world.scrollFrames().find(entry.id); scroll != nullptr) {
        // The bars, on the entry's OWN scissor -- which is the parent's clip,
        // not the region's. A `ScrollFrame` clips its descendants, and a bar
        // clipped by that clip would scroll away with the content it reports on.
        const core::Vec2 size{box.max.x - box.min.x, box.max.y - box.min.y};
        const ScrollAxis horizontal{scroll->canvasSize.x.scale * size.x + scroll->canvasSize.x.offset, size.x,
                                    scroll->canvasPosition.x};
        const ScrollAxis vertical{scroll->canvasSize.y.scale * size.y + scroll->canvasSize.y.offset, size.y,
                                  scroll->canvasPosition.y};
        appendScrollBar(box, vertical, scroll->scrollBarThickness, true, self->backgroundColor, entry.scissor,
                        out.quads);
        appendScrollBar(box, horizontal, scroll->scrollBarThickness, false, self->backgroundColor, entry.scissor,
                        out.quads);
    }

    if (const scene::ImageLabelComponent* image = world.imageLabels().find(entry.id); image != nullptr) {
        if (!image->image.empty()) {
            // A picture the provider cannot resolve -- not loaded, or a URI that
            // names nothing -- draws as the flat tint. That is what an image
            // still arriving looks like, and it is better than a hole.
            ResolvedImage resolved;
            const bool ready = resolveImage(image->image, resolved);
            appendImageQuads(box, resolved, ready, image->scaleType, image->sliceCenter, image->imageColor,
                             entry.scissor, cornerRadius, out.quads);
        }
        return;
    }
    if (const scene::TextLabelComponent* label = world.textLabels().find(entry.id); label != nullptr) {
        std::string_view text = label->text;
        core::Color3 color = label->textColor;
        if (text.empty()) {
            // A focused-away, empty `TextInput` shows its placeholder. Dimmed
            // rather than coloured differently, because a placeholder that
            // looked like real text is a field people fail to fill in.
            const scene::TextInputComponent* input = world.textInputs().find(entry.id);
            if (input == nullptr || input->placeholderText.empty())
                return;
            text = input->placeholderText;
            color = core::Color3{color.r * 0.5f + 0.25f, color.g * 0.5f + 0.25f, color.b * 0.5f + 0.25f};
        }

        // `TextScaled` re-measures at the size that fills the box rather than
        // stretching a bitmap: there is no distance field in v1, and a stretched
        // glyph is what "scaled text" usually looks like.
        f32 size = label->textSize;
        if (label->textScaled && !text.empty()) {
            const TextRunMetrics unit = measureText(text, label->font, 100.0f, 0.0f);
            if (unit.size.x > 0.0f && unit.size.y > 0.0f) {
                size = 100.0f * std::fmin(self->absoluteSize.x / unit.size.x, self->absoluteSize.y / unit.size.y);
            }
        }

        buildTextGeometry(text, label->font, size, label->textWrapped ? self->absoluteSize.x : 0.0f, box,
                          label->horizontalAlignment, label->verticalAlignment, color, 1.0f, entry.scissor, out.quads);

        // --- The caret (S6.7) -------------------------------------------------
        //
        // **A bar where the next character goes**, and only in the field that
        // has focus. Drawn after the text so it is never behind a glyph, and as
        // an ordinary quad so it goes through the same scissor and the same
        // batch -- a caret that needed its own pass would be a second way to
        // draw a rectangle.
        //
        // Measured rather than assumed: the x is the width of the text BEFORE
        // it, so the caret sits between two glyphs however wide they are, and
        // the alignment offset is recomputed the same way `buildTextGeometry`
        // computes it. A single line, because `TextInput` is single-line -- the
        // class doc says so and a wrapped caret is a different feature.
        const scene::TextInputComponent* field = world.textInputs().find(entry.id);
        if (field != nullptr && field->focused) {
            const std::string_view whole = label->text;
            const usize at = std::min(static_cast<usize>(field->caret), whole.size());
            const TextRunMetrics before = measureText(whole.substr(0, at), label->font, size, 0.0f);
            const TextRunMetrics all = measureText(whole, label->font, size, 0.0f);

            f32 x = box.min.x;
            if (label->horizontalAlignment == 1)
                x += (box.max.x - box.min.x - all.size.x) * 0.5f;
            else if (label->horizontalAlignment == 2)
                x += box.max.x - box.min.x - all.size.x;
            x += before.size.x;

            const f32 height = all.size.y > 0.0f ? all.size.y : size;
            f32 y = box.min.y;
            if (label->verticalAlignment == 1)
                y += (box.max.y - box.min.y - height) * 0.5f;
            else if (label->verticalAlignment == 2)
                y += box.max.y - box.min.y - height;

            // A hair over one pixel, so it is visible at every scale without
            // being a glyph in its own right.
            constexpr f32 kCaretWidth = 1.5f;
            DrawQuad caret;
            caret.min = core::Vec2{x, y};
            caret.max = core::Vec2{x + kCaretWidth, y + height};
            caret.color = label->textColor;
            caret.alpha = 1.0f;
            // No texture, so the shader multiplies by white and this is a flat
            // bar -- the same path every solid rectangle in the UI takes.
            caret.scissor = entry.scissor;
            out.quads.push_back(caret);
        }
    }
}

} // namespace

void buildDrawList(const scene::World& world, core::InstanceId uiService, DrawList& out)
{
    out.clear();
    if (!uiService.valid())
        return;

    // Index 0 is the whole window, and it is always present so that "no clip"
    // needs no special case anywhere downstream.
    out.scissors.push_back(Rect{Vec2{-1.0e9f, -1.0e9f}, Vec2{1.0e9f, 1.0e9f}});

    // Every enabled ScreenGui, in `DisplayOrder`. Stable, so two trees at one
    // order keep document order rather than swapping between frames.
    std::vector<std::pair<f32, core::InstanceId>> screens;
    for (core::InstanceId child = world.firstChild(uiService); child.valid(); child = world.nextSibling(child)) {
        if (const scene::ScreenGuiComponent* screen = world.screenGuis().find(child);
            screen != nullptr && screen->enabled) {
            screens.emplace_back(screen->displayOrder, child);
        }
    }
    std::stable_sort(screens.begin(), screens.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Entry> entries;
    for (const auto& [order, screen] : screens) {
        entries.clear();
        for (core::InstanceId element = world.firstChild(screen); element.valid();
             element = world.nextSibling(element)) {
            collect(world, element, 0, entries, out.scissors);
        }

        // `ZIndex` then document order, stably. One flat ordering per tree, and
        // the tie-break is what makes it total: two elements at ZIndex 0 draw in
        // the order they were built, on every run.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry& a, const Entry& b) { return a.zIndex < b.zIndex; });

        for (const Entry& entry : entries)
            emit(world, entry, out);
    }
}

void setImageProvider(ImageProvider provider, void* user) noexcept
{
    g_imageProvider = provider;
    g_imageProviderUser = user;
}

} // namespace luaug::ui
