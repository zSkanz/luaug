#include "luaug/scene/world.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>

namespace luaug::ui {
namespace {

using core::Rect;
using core::UDim;
using core::UDim2;
using core::Vec2;

LayoutStats g_stats;

// `Enum.AutomaticSize`.
constexpr i32 AutoNone = 0;
constexpr i32 AutoX = 1;
constexpr i32 AutoY = 2;
constexpr i32 AutoXY = 3;

// `Enum.FillDirection`, `Enum.HorizontalAlignment`, `Enum.VerticalAlignment`,
// `Enum.SortOrder`. Spelled once rather than compared against literals at the
// four places each is read.
constexpr i32 FillHorizontal = 0;
// `Start` is 0 and is the fall-through case in every alignment branch, so it is
// named in this comment rather than as a constant nothing reads.
constexpr i32 AlignCenter = 1;
constexpr i32 AlignEnd = 2;
constexpr i32 SortByName = 0;

[[nodiscard]] f32 resolve(UDim value, f32 parent) noexcept
{
    return parent * value.scale + value.offset;
}

[[nodiscard]] Vec2 resolve(const UDim2& value, Vec2 parent) noexcept
{
    return Vec2{resolve(value.x, parent.x), resolve(value.y, parent.y)};
}

// The modifiers a parent carries. Looked up once per parent rather than once
// per child: a list of ten children would otherwise scan its parent's children
// ten times looking for the same two instances.
struct Modifiers
{
    const scene::UIListLayoutComponent* list = nullptr;
    const scene::UIPaddingComponent* padding = nullptr;
};

[[nodiscard]] Modifiers modifiersOf(const scene::World& world, core::InstanceId parent)
{
    Modifiers found;
    for (core::InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
        if (found.list == nullptr)
            found.list = world.listLayouts().find(child);
        if (found.padding == nullptr)
            found.padding = world.uiPaddings().find(child);
    }
    return found;
}

// The rectangle a parent offers its children, after its padding. In the
// parent's own pixel space, so `min` is usually not zero.
[[nodiscard]] Rect contentRect(Vec2 origin, Vec2 size, const scene::UIPaddingComponent* padding) noexcept
{
    if (padding == nullptr)
        return Rect{origin, origin + size};

    const f32 left = resolve(padding->paddingLeft, size.x);
    const f32 right = resolve(padding->paddingRight, size.x);
    const f32 top = resolve(padding->paddingTop, size.y);
    const f32 bottom = resolve(padding->paddingBottom, size.y);
    return Rect{Vec2{origin.x + left, origin.y + top}, Vec2{origin.x + size.x - right, origin.y + size.y - bottom}};
}

[[nodiscard]] Vec2 extentOf(Rect rect) noexcept
{
    return Vec2{rect.max.x - rect.min.x, rect.max.y - rect.min.y};
}

// The `UIObject` children of a parent, in the order a layout should walk them.
//
// Document order by default, which is child order and therefore reproducible
// (R10). `SortOrder` re-sorts it, and the sort is STABLE so that ties keep
// document order rather than whatever the comparison happened to decide.
void collectChildren(const scene::World& world, core::InstanceId parent, const scene::UIListLayoutComponent* list,
                     std::vector<core::InstanceId>& out)
{
    out.clear();
    for (core::InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
        if (world.uiObjects().find(child) != nullptr)
            out.push_back(child);
    }
    if (list == nullptr)
        return;

    if (list->sortOrder == SortByName) {
        std::stable_sort(out.begin(), out.end(), [&world](core::InstanceId a, core::InstanceId b) {
            return world.atoms().text(world.name(a)) < world.atoms().text(world.name(b));
        });
        return;
    }
    std::stable_sort(out.begin(), out.end(), [&world](core::InstanceId a, core::InstanceId b) {
        return world.uiObjects().find(a)->layoutOrder < world.uiObjects().find(b)->layoutOrder;
    });
}

// The size a `TextLabel` wants, or nothing for an element that is not one.
[[nodiscard]] Vec2 textExtent(const scene::World& world, core::InstanceId id, f32 wrapWidth)
{
    const scene::TextLabelComponent* label = world.textLabels().find(id);
    if (label == nullptr || label->text.empty())
        return Vec2{};
    const TextRunMetrics metrics =
        measureText(label->text, label->font, label->textSize, label->textWrapped ? wrapWidth : 0.0f);
    return metrics.size;
}

struct Pass
{
    const scene::World& world;
    std::vector<core::InstanceId> scratch;

    // Bottom-up: what this element WANTS to be, in pixels, given the space its
    // parent can offer. Only the axes `AutomaticSize` covers are computed;
    // the rest come from `Size` in the top-down pass and are passed in here so
    // that a wrapped label knows how wide it may be.
    [[nodiscard]] Vec2 desiredSize(core::InstanceId id, Vec2 available)
    {
        const scene::UIObjectComponent* self = world.uiObjects().find(id);
        if (self == nullptr)
            return Vec2{};

        Vec2 fixed = resolve(self->size, available);
        if (self->automaticSize == AutoNone)
            return fixed;

        const Modifiers mods = modifiersOf(world, id);

        // The content this element has to hold: its text, or the extent of its
        // children laid out inside it.
        Vec2 content = textExtent(world, id, fixed.x);

        std::vector<core::InstanceId> children;
        collectChildren(world, id, mods.list, children);
        if (!children.empty()) {
            Vec2 childExtent;
            if (mods.list != nullptr) {
                const f32 gap =
                    resolve(mods.list->padding, mods.list->fillDirection == FillHorizontal ? fixed.x : fixed.y);
                for (usize index = 0; index < children.size(); ++index) {
                    const Vec2 want = desiredSize(children[index], fixed);
                    if (mods.list->fillDirection == FillHorizontal) {
                        childExtent.x += want.x + (index == 0 ? 0.0f : gap);
                        childExtent.y = std::fmax(childExtent.y, want.y);
                    }
                    else {
                        childExtent.y += want.y + (index == 0 ? 0.0f : gap);
                        childExtent.x = std::fmax(childExtent.x, want.x);
                    }
                }
            }
            else {
                // Without a list layout, children are placed absolutely, so the
                // extent is the furthest any of them reaches -- position and
                // size together, because a child at (0.9, 0) is what makes a
                // parent wide.
                for (const core::InstanceId child : children) {
                    const scene::UIObjectComponent* component = world.uiObjects().find(child);
                    const Vec2 want = desiredSize(child, fixed);
                    const Vec2 at = resolve(component->position, fixed) -
                                    Vec2{component->anchorPoint.x * want.x, component->anchorPoint.y * want.y};
                    childExtent.x = std::fmax(childExtent.x, at.x + want.x);
                    childExtent.y = std::fmax(childExtent.y, at.y + want.y);
                }
            }
            content.x = std::fmax(content.x, childExtent.x);
            content.y = std::fmax(content.y, childExtent.y);
        }

        // Padding is around the content, so it is added rather than subtracted
        // on this pass -- the one place the two directions of the same property
        // read differently, and the one worth a sentence.
        if (mods.padding != nullptr) {
            content.x += resolve(mods.padding->paddingLeft, fixed.x) + resolve(mods.padding->paddingRight, fixed.x);
            content.y += resolve(mods.padding->paddingTop, fixed.y) + resolve(mods.padding->paddingBottom, fixed.y);
        }

        if (self->automaticSize == AutoX || self->automaticSize == AutoXY)
            fixed.x = content.x;
        if (self->automaticSize == AutoY || self->automaticSize == AutoXY)
            fixed.y = content.y;
        return fixed;
    }
};

// Top-down: assign this element's rectangle, then its children's.
void place(scene::World& world, Pass& pass, core::InstanceId id, Vec2 parentOrigin, Vec2 parentSize, Vec2 forcedSize,
           bool useForced)
{
    scene::UIObjectComponent* self = world.uiObjects().find(id);
    if (self == nullptr || !self->visible)
        return;

    // `useForced` is how a list layout hands a child the slot it computed: the
    // size AND the top-left are the layout's, and the child's own `Position` and
    // `AnchorPoint` are not consulted. A child inside a layout does not place
    // itself -- that is what a layout is.
    const Vec2 size = useForced ? forcedSize : pass.desiredSize(id, parentSize);
    const Vec2 topLeft = useForced ? parentOrigin
                                   : parentOrigin + resolve(self->position, parentSize) -
                                         Vec2{self->anchorPoint.x * size.x, self->anchorPoint.y * size.y};

    self->absolutePosition = topLeft;
    self->absoluteSize = size;
    ++g_stats.elementsLaidOut;

    const Modifiers mods = modifiersOf(world, id);
    Rect content = contentRect(topLeft, size, mods.padding);

    // A ScrollFrame's children lay out against its canvas, shifted by how far
    // it has been scrolled. Clamped here rather than at the write, so a script
    // that scrolls past the end settles at the end on the next layout instead
    // of showing emptiness.
    if (scene::ScrollFrameComponent* scroll = world.scrollFrames().find(id); scroll != nullptr) {
        const Vec2 canvas = resolve(scroll->canvasSize, size);
        const Vec2 room{std::fmax(0.0f, canvas.x - size.x), std::fmax(0.0f, canvas.y - size.y)};
        scroll->canvasPosition = Vec2{std::clamp(scroll->canvasPosition.x, 0.0f, room.x),
                                      std::clamp(scroll->canvasPosition.y, 0.0f, room.y)};
        content = Rect{content.min - scroll->canvasPosition,
                       content.min - scroll->canvasPosition + (canvas.x > 0.0f || canvas.y > 0.0f ? canvas : size)};
    }

    std::vector<core::InstanceId> children;
    collectChildren(world, id, mods.list, children);
    if (children.empty())
        return;

    const Vec2 contentSize = extentOf(content);

    if (mods.list == nullptr) {
        for (const core::InstanceId child : children)
            place(world, pass, child, content.min, contentSize, Vec2{}, false);
        return;
    }

    // A single-axis stack. `Wraps` breaks it into rows when the line runs out
    // of room, which is the one case where an element's position depends on the
    // width of its predecessors rather than only on its own properties.
    const bool horizontal = mods.list->fillDirection == FillHorizontal;
    const f32 gap = resolve(mods.list->padding, horizontal ? contentSize.x : contentSize.y);
    const f32 lineLimit = horizontal ? contentSize.x : contentSize.y;

    // Measured first, so the cross-axis alignment has a total to work from. A
    // stack that aligned as it went could not centre itself.
    std::vector<Vec2> sizes;
    sizes.reserve(children.size());
    for (const core::InstanceId child : children)
        sizes.push_back(pass.desiredSize(child, contentSize));

    usize lineStart = 0;
    f32 lineOffset = 0.0f;
    while (lineStart < children.size()) {
        usize lineEnd = lineStart;
        f32 lineMain = 0.0f;
        f32 lineCross = 0.0f;
        while (lineEnd < children.size()) {
            const f32 main = horizontal ? sizes[lineEnd].x : sizes[lineEnd].y;
            const f32 next = lineMain + main + (lineEnd == lineStart ? 0.0f : gap);
            if (mods.list->wraps && lineEnd != lineStart && next > lineLimit)
                break;
            lineMain = next;
            lineCross = std::fmax(lineCross, horizontal ? sizes[lineEnd].y : sizes[lineEnd].x);
            ++lineEnd;
        }

        const i32 mainAlign = horizontal ? mods.list->horizontalAlignment : mods.list->verticalAlignment;
        f32 cursor = 0.0f;
        if (mainAlign == AlignCenter)
            cursor = (lineLimit - lineMain) * 0.5f;
        else if (mainAlign == AlignEnd)
            cursor = lineLimit - lineMain;

        const i32 crossAlign = horizontal ? mods.list->verticalAlignment : mods.list->horizontalAlignment;
        for (usize index = lineStart; index < lineEnd; ++index) {
            const Vec2 childSize = sizes[index];
            const f32 main = horizontal ? childSize.x : childSize.y;
            const f32 cross = horizontal ? childSize.y : childSize.x;

            f32 crossOffset = lineOffset;
            if (crossAlign == AlignCenter)
                crossOffset += (lineCross - cross) * 0.5f;
            else if (crossAlign == AlignEnd)
                crossOffset += lineCross - cross;

            const Vec2 at = horizontal ? Vec2{content.min.x + cursor, content.min.y + crossOffset}
                                       : Vec2{content.min.x + crossOffset, content.min.y + cursor};
            place(world, pass, children[index], at, contentSize, childSize, true);
            cursor += main + gap;
        }

        lineOffset += lineCross + gap;
        lineStart = lineEnd;
        if (!mods.list->wraps)
            break;
    }
}

} // namespace

void layout(scene::World& world, core::InstanceId uiService, core::Vec2 windowSize)
{
    if (!uiService.valid())
        return;

    Pass pass{world, {}};

    // Document order over `UIService`'s children, which is where every
    // `ScreenGui` lives. Not sorted by `DisplayOrder` here: that orders DRAWING
    // and two trees never affect each other's layout, so sorting would be work
    // with no observable result.
    for (core::InstanceId child = world.firstChild(uiService); child.valid(); child = world.nextSibling(child)) {
        scene::ScreenGuiComponent* screen = world.screenGuis().find(child);
        if (screen == nullptr || !screen->enabled || !screen->layoutDirty)
            continue;

        ++g_stats.solverRuns;
        screen->layoutDirty = false;

        // The insets are the window's, and a tree opts out of them with
        // `ScreenInsets = false` -- a full-bleed background is the exception
        // and a HUD clipped by a notch is the failure.
        Vec2 origin;
        Vec2 available = windowSize;
        if (screen->screenInsets) {
            const core::Rect& insets = world.engineState().safeAreaInsets;
            origin = insets.min;
            available = Vec2{windowSize.x - insets.min.x - insets.max.x, windowSize.y - insets.min.y - insets.max.y};
        }

        for (core::InstanceId element = world.firstChild(child); element.valid();
             element = world.nextSibling(element)) {
            place(world, pass, element, origin, available, Vec2{}, false);
        }
    }
}

const LayoutStats& layoutStats() noexcept
{
    return g_stats;
}

void resetLayoutStats() noexcept
{
    g_stats = LayoutStats{};
}

} // namespace luaug::ui
