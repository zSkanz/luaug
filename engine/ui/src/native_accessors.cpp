// The hand-written half of `ui`'s reflection (architecture.md §4).
//
// Forty-five properties across thirteen classes, and almost every one of them
// stores a value and marks a layout dirty. The two that are not boilerplate are
// worth finding: `AbsolutePosition` and `AbsoluteSize` have no setter at all,
// because they are the solver's OUTPUT -- writing one would be arguing with the
// layout rather than changing it.
//
// **The dirty marking is the design and not an optimisation.** A write that
// changes what the solver would produce walks up to the nearest `ScreenGui` and
// marks it; a purely visual write does not. That is what makes "a screen nothing
// changed runs no solver" a fact rather than a hope, and it is why the
// milestone's benchmark asserts ZERO solver invocations on an idle frame --
// "about zero microseconds" would be a measurement of the clock rather than of
// the engine.
#include "luaug/scene/world.h"
#include "luaug/ui/scene_types.h"

// By a path relative to this file rather than through an include directory:
// every module's generated header has the same name, so two of them on one
// include path would resolve by search order. `scene`, `render` and `input` all
// reach theirs the same way.
#include <cmath>

#include "../generated/class_descriptors.gen.h"

namespace luaug::ui {
namespace native {
namespace {

using core::f32;
using core::f64;
using scene::Value;

[[nodiscard]] bool isFinite(f64 value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool isFinite(core::Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool isFinite(core::UDim value) noexcept
{
    return std::isfinite(value.scale) && std::isfinite(value.offset);
}

// Up the tree to the `ScreenGui` this element belongs to, marking it dirty.
//
// Walked rather than cached, and the walk is what makes a UI tree's depth the
// cost of a layout-affecting write. Ten levels is a deep UI and ten parent
// lookups is nothing; a cached pointer would be a second thing to invalidate on
// every reparent, which is the bookkeeping that goes wrong quietly.
//
// An element outside any ScreenGui marks nothing, and that is correct: it is not
// laid out, so there is no layout to redo.
void markLayoutDirty(scene::World& world, core::InstanceId id)
{
    for (core::InstanceId current = id; current.valid(); current = world.parentOf(current)) {
        if (scene::ScreenGuiComponent* screen = world.screenGuis().find(current); screen != nullptr) {
            screen->layoutDirty = true;
            return;
        }
    }
}

// An enum write, checked against the right enum AND against the item list, the
// same way `input`'s is. The id comes from the generated header rather than from
// a lookup by name: a property write should not pay a hash probe for a number
// the generator already decided.
[[nodiscard]] bool takeEnum(const scene::World& world, const Value& value, scene::EnumId enumId, core::i32& out)
{
    const auto* item = std::get_if<scene::EnumValue>(&value);
    if (item == nullptr || item->enumId != enumId)
        return false;
    if (world.enums().findValue(enumId, item->value) == nullptr)
        return false;
    out = item->value;
    return true;
}

} // namespace

// --- ScreenGui -------------------------------------------------------------

Value getScreenGuiEnabled(const scene::World& world, core::InstanceId id)
{
    const scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    return component == nullptr ? Value{} : Value{component->enabled};
}

bool setScreenGuiEnabled(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->enabled = *flag;
    markLayoutDirty(world, id);
    return true;
}

Value getScreenGuiDisplayOrder(const scene::World& world, core::InstanceId id)
{
    const scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    return component == nullptr ? Value{} : Value{component->displayOrder};
}

bool setScreenGuiDisplayOrder(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->displayOrder = static_cast<f32>(*number);
    return true;
}

Value getScreenGuiScreenInsets(const scene::World& world, core::InstanceId id)
{
    const scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    return component == nullptr ? Value{} : Value{component->screenInsets};
}

bool setScreenGuiScreenInsets(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScreenGuiComponent* component = world.screenGuis().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->screenInsets = *flag;
    markLayoutDirty(world, id);
    return true;
}

// --- UIObject --------------------------------------------------------------

Value getUIObjectPosition(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->position};
}

bool setUIObjectPosition(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* udim2 = std::get_if<core::UDim2>(&value);
    if (udim2 == nullptr || !isFinite(udim2->x) || !isFinite(udim2->y))
        return false;
    component->position = *udim2;
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectSize(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->size};
}

bool setUIObjectSize(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* udim2 = std::get_if<core::UDim2>(&value);
    if (udim2 == nullptr || !isFinite(udim2->x) || !isFinite(udim2->y))
        return false;
    component->size = *udim2;
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectAnchorPoint(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->anchorPoint};
}

bool setUIObjectAnchorPoint(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* point = std::get_if<core::Vec2>(&value);
    if (point == nullptr || !isFinite(*point))
        return false;
    component->anchorPoint = *point;
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectRotation(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->rotation};
}

bool setUIObjectRotation(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->rotation = static_cast<f32>(*number);
    return true;
}

Value getUIObjectBackgroundColor(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->backgroundColor};
}

bool setUIObjectBackgroundColor(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* color = std::get_if<core::Color3>(&value);
    if (color == nullptr)
        return false;
    component->backgroundColor = *color;
    return true;
}

Value getUIObjectBackgroundTransparency(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->backgroundTransparency};
}

bool setUIObjectBackgroundTransparency(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->backgroundTransparency = static_cast<f32>(*number);
    return true;
}

Value getUIObjectVisible(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->visible};
}

bool setUIObjectVisible(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->visible = *flag;
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectZIndex(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->zIndex};
}

bool setUIObjectZIndex(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->zIndex = static_cast<f32>(*number);
    return true;
}

Value getUIObjectLayoutOrder(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->layoutOrder};
}

bool setUIObjectLayoutOrder(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->layoutOrder = static_cast<f32>(*number);
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectAutomaticSize(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{}
                                : Value{scene::EnumValue{generated::AutomaticSizeEnumId, component->automaticSize}};
}

bool setUIObjectAutomaticSize(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::AutomaticSizeEnumId, item))
        return false;
    component->automaticSize = item;
    markLayoutDirty(world, id);
    return true;
}

Value getUIObjectClipsDescendants(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->clipsDescendants};
}

bool setUIObjectClipsDescendants(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIObjectComponent* component = world.uiObjects().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->clipsDescendants = *flag;
    return true;
}

Value getUIObjectAbsolutePosition(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->absolutePosition};
}

Value getUIObjectAbsoluteSize(const scene::World& world, core::InstanceId id)
{
    const scene::UIObjectComponent* component = world.uiObjects().find(id);
    return component == nullptr ? Value{} : Value{component->absoluteSize};
}

// --- TextLabel -------------------------------------------------------------

Value getTextLabelText(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->text};
}

bool setTextLabelText(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    component->text = *text;
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelTextColor(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->textColor};
}

bool setTextLabelTextColor(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* color = std::get_if<core::Color3>(&value);
    if (color == nullptr)
        return false;
    component->textColor = *color;
    return true;
}

Value getTextLabelTextSize(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->textSize};
}

bool setTextLabelTextSize(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->textSize = static_cast<f32>(*number);
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelFont(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->font};
}

bool setTextLabelFont(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    component->font = *text;
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelHorizontalAlignment(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr
               ? Value{}
               : Value{scene::EnumValue{generated::HorizontalAlignmentEnumId, component->horizontalAlignment}};
}

bool setTextLabelHorizontalAlignment(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::HorizontalAlignmentEnumId, item))
        return false;
    component->horizontalAlignment = item;
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelVerticalAlignment(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr
               ? Value{}
               : Value{scene::EnumValue{generated::VerticalAlignmentEnumId, component->verticalAlignment}};
}

bool setTextLabelVerticalAlignment(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::VerticalAlignmentEnumId, item))
        return false;
    component->verticalAlignment = item;
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelTextWrapped(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->textWrapped};
}

bool setTextLabelTextWrapped(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->textWrapped = *flag;
    markLayoutDirty(world, id);
    return true;
}

Value getTextLabelTextScaled(const scene::World& world, core::InstanceId id)
{
    const scene::TextLabelComponent* component = world.textLabels().find(id);
    return component == nullptr ? Value{} : Value{component->textScaled};
}

bool setTextLabelTextScaled(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextLabelComponent* component = world.textLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->textScaled = *flag;
    markLayoutDirty(world, id);
    return true;
}

// --- TextInput -------------------------------------------------------------

Value getTextInputPlaceholderText(const scene::World& world, core::InstanceId id)
{
    const scene::TextInputComponent* component = world.textInputs().find(id);
    return component == nullptr ? Value{} : Value{component->placeholderText};
}

bool setTextInputPlaceholderText(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::TextInputComponent* component = world.textInputs().find(id);
    if (component == nullptr)
        return false;
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    component->placeholderText = *text;
    markLayoutDirty(world, id);
    return true;
}

// --- ImageLabel ------------------------------------------------------------

Value getImageLabelImage(const scene::World& world, core::InstanceId id)
{
    const scene::ImageLabelComponent* component = world.imageLabels().find(id);
    return component == nullptr ? Value{} : Value{component->image};
}

bool setImageLabelImage(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ImageLabelComponent* component = world.imageLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    component->image = *text;
    return true;
}

Value getImageLabelImageColor(const scene::World& world, core::InstanceId id)
{
    const scene::ImageLabelComponent* component = world.imageLabels().find(id);
    return component == nullptr ? Value{} : Value{component->imageColor};
}

bool setImageLabelImageColor(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ImageLabelComponent* component = world.imageLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* color = std::get_if<core::Color3>(&value);
    if (color == nullptr)
        return false;
    component->imageColor = *color;
    return true;
}

Value getImageLabelScaleType(const scene::World& world, core::InstanceId id)
{
    const scene::ImageLabelComponent* component = world.imageLabels().find(id);
    return component == nullptr ? Value{} : Value{scene::EnumValue{generated::ScaleTypeEnumId, component->scaleType}};
}

bool setImageLabelScaleType(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ImageLabelComponent* component = world.imageLabels().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::ScaleTypeEnumId, item))
        return false;
    component->scaleType = item;
    return true;
}

Value getImageLabelSliceCenter(const scene::World& world, core::InstanceId id)
{
    const scene::ImageLabelComponent* component = world.imageLabels().find(id);
    return component == nullptr ? Value{} : Value{component->sliceCenter};
}

bool setImageLabelSliceCenter(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ImageLabelComponent* component = world.imageLabels().find(id);
    if (component == nullptr)
        return false;
    const auto* rect = std::get_if<core::Rect>(&value);
    if (rect == nullptr || !isFinite(rect->min) || !isFinite(rect->max))
        return false;
    component->sliceCenter = *rect;
    return true;
}

// --- ScrollFrame -----------------------------------------------------------

Value getScrollFrameCanvasSize(const scene::World& world, core::InstanceId id)
{
    const scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    return component == nullptr ? Value{} : Value{component->canvasSize};
}

bool setScrollFrameCanvasSize(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    if (component == nullptr)
        return false;
    const auto* udim2 = std::get_if<core::UDim2>(&value);
    if (udim2 == nullptr || !isFinite(udim2->x) || !isFinite(udim2->y))
        return false;
    component->canvasSize = *udim2;
    markLayoutDirty(world, id);
    return true;
}

Value getScrollFrameCanvasPosition(const scene::World& world, core::InstanceId id)
{
    const scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    return component == nullptr ? Value{} : Value{component->canvasPosition};
}

bool setScrollFrameCanvasPosition(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    if (component == nullptr)
        return false;
    const auto* point = std::get_if<core::Vec2>(&value);
    if (point == nullptr || !isFinite(*point))
        return false;
    component->canvasPosition = *point;
    markLayoutDirty(world, id);
    return true;
}

Value getScrollFrameScrollBarThickness(const scene::World& world, core::InstanceId id)
{
    const scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    return component == nullptr ? Value{} : Value{component->scrollBarThickness};
}

bool setScrollFrameScrollBarThickness(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::ScrollFrameComponent* component = world.scrollFrames().find(id);
    if (component == nullptr)
        return false;
    const auto* number = std::get_if<f64>(&value);
    if (number == nullptr || !isFinite(*number))
        return false;
    component->scrollBarThickness = static_cast<f32>(*number);
    markLayoutDirty(world, id);
    return true;
}

// --- UIListLayout ----------------------------------------------------------

Value getUIListLayoutFillDirection(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr ? Value{}
                                : Value{scene::EnumValue{generated::FillDirectionEnumId, component->fillDirection}};
}

bool setUIListLayoutFillDirection(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::FillDirectionEnumId, item))
        return false;
    component->fillDirection = item;
    markLayoutDirty(world, id);
    return true;
}

Value getUIListLayoutPadding(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr ? Value{} : Value{component->padding};
}

bool setUIListLayoutPadding(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->padding = *udim;
    markLayoutDirty(world, id);
    return true;
}

Value getUIListLayoutHorizontalAlignment(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr
               ? Value{}
               : Value{scene::EnumValue{generated::HorizontalAlignmentEnumId, component->horizontalAlignment}};
}

bool setUIListLayoutHorizontalAlignment(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::HorizontalAlignmentEnumId, item))
        return false;
    component->horizontalAlignment = item;
    markLayoutDirty(world, id);
    return true;
}

Value getUIListLayoutVerticalAlignment(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr
               ? Value{}
               : Value{scene::EnumValue{generated::VerticalAlignmentEnumId, component->verticalAlignment}};
}

bool setUIListLayoutVerticalAlignment(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::VerticalAlignmentEnumId, item))
        return false;
    component->verticalAlignment = item;
    markLayoutDirty(world, id);
    return true;
}

Value getUIListLayoutSortOrder(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr ? Value{} : Value{scene::EnumValue{generated::SortOrderEnumId, component->sortOrder}};
}

bool setUIListLayoutSortOrder(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    core::i32 item = 0;
    if (!takeEnum(world, value, generated::SortOrderEnumId, item))
        return false;
    component->sortOrder = item;
    markLayoutDirty(world, id);
    return true;
}

Value getUIListLayoutWraps(const scene::World& world, core::InstanceId id)
{
    const scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    return component == nullptr ? Value{} : Value{component->wraps};
}

bool setUIListLayoutWraps(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIListLayoutComponent* component = world.listLayouts().find(id);
    if (component == nullptr)
        return false;
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    component->wraps = *flag;
    markLayoutDirty(world, id);
    return true;
}

// --- UIPadding -------------------------------------------------------------

Value getUIPaddingPaddingTop(const scene::World& world, core::InstanceId id)
{
    const scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    return component == nullptr ? Value{} : Value{component->paddingTop};
}

bool setUIPaddingPaddingTop(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->paddingTop = *udim;
    markLayoutDirty(world, id);
    return true;
}

Value getUIPaddingPaddingBottom(const scene::World& world, core::InstanceId id)
{
    const scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    return component == nullptr ? Value{} : Value{component->paddingBottom};
}

bool setUIPaddingPaddingBottom(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->paddingBottom = *udim;
    markLayoutDirty(world, id);
    return true;
}

Value getUIPaddingPaddingLeft(const scene::World& world, core::InstanceId id)
{
    const scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    return component == nullptr ? Value{} : Value{component->paddingLeft};
}

bool setUIPaddingPaddingLeft(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->paddingLeft = *udim;
    markLayoutDirty(world, id);
    return true;
}

Value getUIPaddingPaddingRight(const scene::World& world, core::InstanceId id)
{
    const scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    return component == nullptr ? Value{} : Value{component->paddingRight};
}

bool setUIPaddingPaddingRight(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UIPaddingComponent* component = world.uiPaddings().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->paddingRight = *udim;
    markLayoutDirty(world, id);
    return true;
}

// --- UICorner --------------------------------------------------------------

Value getUICornerCornerRadius(const scene::World& world, core::InstanceId id)
{
    const scene::UICornerComponent* component = world.uiCorners().find(id);
    return component == nullptr ? Value{} : Value{component->cornerRadius};
}

bool setUICornerCornerRadius(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::UICornerComponent* component = world.uiCorners().find(id);
    if (component == nullptr)
        return false;
    const auto* udim = std::get_if<core::UDim>(&value);
    if (udim == nullptr || !isFinite(*udim))
        return false;
    component->cornerRadius = *udim;
    return true;
}

// --- Storage ----------------------------------------------------------------
//
// One pair per class that declares its own components. `World::create` walks the
// ancestry root-first and calls every hook it finds, so a `TextButton` gets both
// the `UIObject` pair and the `TextLabel` pair without either naming the other.

void attachScreenGuiComponents(scene::World& world, core::InstanceId id)
{
    world.screenGuis().add(id, scene::ScreenGuiComponent{});
}

void detachScreenGuiComponents(scene::World& world, core::InstanceId id)
{
    world.screenGuis().remove(id);
}

void attachUIObjectComponents(scene::World& world, core::InstanceId id)
{
    world.uiObjects().add(id, scene::UIObjectComponent{});
}

void detachUIObjectComponents(scene::World& world, core::InstanceId id)
{
    world.uiObjects().remove(id);
}

void attachTextLabelComponents(scene::World& world, core::InstanceId id)
{
    world.textLabels().add(id, scene::TextLabelComponent{});
}

void detachTextLabelComponents(scene::World& world, core::InstanceId id)
{
    world.textLabels().remove(id);
}

void attachTextInputComponents(scene::World& world, core::InstanceId id)
{
    world.textInputs().add(id, scene::TextInputComponent{});
}

void detachTextInputComponents(scene::World& world, core::InstanceId id)
{
    world.textInputs().remove(id);
}

void attachImageLabelComponents(scene::World& world, core::InstanceId id)
{
    world.imageLabels().add(id, scene::ImageLabelComponent{});
}

void detachImageLabelComponents(scene::World& world, core::InstanceId id)
{
    world.imageLabels().remove(id);
}

void attachScrollFrameComponents(scene::World& world, core::InstanceId id)
{
    world.scrollFrames().add(id, scene::ScrollFrameComponent{});
}

void detachScrollFrameComponents(scene::World& world, core::InstanceId id)
{
    world.scrollFrames().remove(id);
}

void attachUIListLayoutComponents(scene::World& world, core::InstanceId id)
{
    world.listLayouts().add(id, scene::UIListLayoutComponent{});
}

void detachUIListLayoutComponents(scene::World& world, core::InstanceId id)
{
    world.listLayouts().remove(id);
}

void attachUIPaddingComponents(scene::World& world, core::InstanceId id)
{
    world.uiPaddings().add(id, scene::UIPaddingComponent{});
}

void detachUIPaddingComponents(scene::World& world, core::InstanceId id)
{
    world.uiPaddings().remove(id);
}

void attachUICornerComponents(scene::World& world, core::InstanceId id)
{
    world.uiCorners().add(id, scene::UICornerComponent{});
}

void detachUICornerComponents(scene::World& world, core::InstanceId id)
{
    world.uiCorners().remove(id);
}

// --- UIService --------------------------------------------------------------
//
// Two numbers about the WINDOW rather than about any instance, so they live in
// `EngineState` beside the scheduler's -- the same arrangement
// `PhysicsService.FixedTimestep` uses, and for the same reason: there is exactly
// one of each service in a world.

Value getUIServiceSafeAreaInsets(const scene::World& world, core::InstanceId)
{
    return Value{world.engineState().safeAreaInsets};
}

Value getUIServiceDisplayScale(const scene::World& world, core::InstanceId)
{
    return Value{static_cast<f64>(world.engineState().displayScale)};
}

} // namespace native

void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms)
{
    generated::registerClasses(classes, atoms);
}

} // namespace luaug::ui
