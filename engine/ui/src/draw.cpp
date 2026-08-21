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
    if (backgroundAlpha > 0.0f) {
        DrawQuad quad;
        quad.min = box.min;
        quad.max = box.max;
        quad.color = self->backgroundColor;
        quad.alpha = backgroundAlpha;
        quad.scissor = entry.scissor;
        out.quads.push_back(quad);
    }

    if (const scene::ImageLabelComponent* image = world.imageLabels().find(entry.id); image != nullptr) {
        // `Image` names an asset the pipeline cannot hand over until M7, so the
        // tint is drawn as a flat quad rather than as a textured one. The
        // property is marked `Inert` in the IDL with that milestone named --
        // which is the difference between a documented gap and a silent one.
        if (!image->image.empty()) {
            DrawQuad quad;
            quad.min = box.min;
            quad.max = box.max;
            quad.color = image->imageColor;
            quad.alpha = backgroundAlpha > 0.0f ? 1.0f : 1.0f;
            quad.scissor = entry.scissor;
            out.quads.push_back(quad);
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

} // namespace luaug::ui
