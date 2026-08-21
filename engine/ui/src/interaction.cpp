// Hit-testing, hover, activation and text focus (api-design.md §2.2's
// `UIObject` signals and `TextInput`; architecture.md §2's `ui`).
//
// Everything here reads rectangles the layout already produced and computes no
// layout of its own. That separation is what makes "what is on screen" and
// "what the pointer is over" the same answer: they come from one set of
// numbers, written once per frame.
//
// The events are enqueued as POD facts on the world's change queue, exactly as
// a property write is, and `script` turns them into deferred fires. Nothing in
// this file knows what a Luau value is (R17).
#include "luaug/scene/world.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace luaug::ui {
namespace {

using core::Rect;
using core::Vec2;

// Frame-to-frame interaction state.
//
// Held here rather than in a component for one reason: none of it is world
// state. A snapshot that restored "the pointer was hovering this button" would
// be restoring a fact about a mouse, and a replay has no mouse -- it has the
// input stream, which produces the hover again on its own.
struct InteractionState
{
    core::InstanceId hovered;
    // What the press started on. `Activated` needs both ends on the same
    // element: a press that slid off a button and released elsewhere is a
    // cancelled press, which is what every UI in the world does and what people
    // rely on to change their minds.
    core::InstanceId pressedOn;
    core::InstanceId focused;
};

InteractionState g_state;

[[nodiscard]] bool contains(Rect box, Vec2 point) noexcept
{
    return point.x >= box.min.x && point.x < box.max.x && point.y >= box.min.y && point.y < box.max.y;
}

void fire(scene::World& world, core::InstanceId subject, const char* event)
{
    if (!subject.valid() || !world.alive(subject))
        return;
    world.changes().push(
        scene::Change{scene::ChangeKind::InstanceEventNoArgs, subject, {}, world.atoms().intern(event)});
}

// Depth-first, in draw order, keeping the LAST hit: the element drawn on top is
// the one the pointer is over, and draw order is `ZIndex` then document order.
// Walking the tree twice -- once to draw and once to hit-test -- is what keeps
// the two from disagreeing about which is on top.
void probe(const scene::World& world, core::InstanceId id, Vec2 point, Rect clip, core::InstanceId& best, f32& bestZ)
{
    const scene::UIObjectComponent* self = world.uiObjects().find(id);
    if (self == nullptr || !self->visible)
        return;

    const Rect box{self->absolutePosition, self->absolutePosition + self->absoluteSize};
    Rect childClip = clip;
    if (self->clipsDescendants || world.scrollFrames().find(id) != nullptr) {
        childClip = Rect{Vec2{std::fmax(box.min.x, clip.min.x), std::fmax(box.min.y, clip.min.y)},
                         Vec2{std::fmin(box.max.x, clip.max.x), std::fmin(box.max.y, clip.max.y)}};
    }

    // Clipped OUT means not hit, which is the whole reason the clip is threaded
    // through: an element scrolled off the end of a list must not answer a
    // click that lands where it would have been.
    if (contains(box, point) && contains(clip, point) && self->zIndex >= bestZ) {
        best = id;
        bestZ = self->zIndex;
    }

    for (core::InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child))
        probe(world, child, point, childClip, best, bestZ);
}

// Appends one UTF-8 sequence, or removes one. Bytes rather than codepoints
// everywhere except here, where a backspace has to remove a whole sequence --
// dropping one byte of a two-byte character leaves a string no renderer can
// read.
void edit(std::string& text, std::string_view added, bool backspace)
{
    if (backspace && !text.empty()) {
        usize cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
            --cut;
        text.erase(cut);
    }
    if (!added.empty())
        text.append(added);
}

} // namespace

core::InstanceId hitTest(const scene::World& world, core::InstanceId uiService, core::Vec2 point)
{
    if (!uiService.valid())
        return {};

    // Screens in `DisplayOrder`, so the topmost tree wins outright: a modal over
    // a HUD takes the click even where the HUD has a button.
    std::vector<std::pair<f32, core::InstanceId>> screens;
    for (core::InstanceId child = world.firstChild(uiService); child.valid(); child = world.nextSibling(child)) {
        if (const scene::ScreenGuiComponent* screen = world.screenGuis().find(child);
            screen != nullptr && screen->enabled) {
            screens.emplace_back(screen->displayOrder, child);
        }
    }
    std::stable_sort(screens.begin(), screens.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    const Rect whole{Vec2{-1.0e9f, -1.0e9f}, Vec2{1.0e9f, 1.0e9f}};
    core::InstanceId best;
    for (const auto& [order, screen] : screens) {
        core::InstanceId hit;
        f32 bestZ = -1.0e30f;
        for (core::InstanceId element = world.firstChild(screen); element.valid();
             element = world.nextSibling(element)) {
            probe(world, element, point, whole, hit, bestZ);
        }
        if (hit.valid())
            best = hit;
    }
    return best;
}

InteractionResult updateInteraction(scene::World& world, core::InstanceId uiService, const InteractionInput& input)
{
    InteractionResult result;
    if (!uiService.valid())
        return result;

    const core::InstanceId over = hitTest(world, uiService, input.pointer);
    result.pointerOverUi = over.valid();

    // Hover, as a pair of edges rather than a state a handler has to compare
    // against its own memory.
    if (over != g_state.hovered) {
        fire(world, g_state.hovered, "PointerExited");
        fire(world, over, "PointerEntered");
        g_state.hovered = over;
    }

    if (input.pressed) {
        g_state.pressedOn = over;

        // Focus moves on the press, and moves AWAY on a press that lands
        // anywhere else -- including on nothing. A field that kept focus after
        // the player clicked the world would go on eating their keystrokes.
        const core::InstanceId wanted = world.textInputs().find(over) != nullptr ? over : core::InstanceId{};
        if (wanted != g_state.focused) {
            if (scene::TextInputComponent* previous = world.textInputs().find(g_state.focused); previous != nullptr) {
                previous->focused = false;
                fire(world, g_state.focused, "FocusLost");
            }
            g_state.focused = wanted;
            if (scene::TextInputComponent* next = world.textInputs().find(wanted); next != nullptr) {
                next->focused = true;
                fire(world, wanted, "Focused");
            }
        }
    }

    if (input.released) {
        // Both ends on the same element, which is what makes a press
        // cancellable by sliding off it.
        if (over.valid() && over == g_state.pressedOn)
            fire(world, over, "Activated");
        g_state.pressedOn = {};
    }

    if (g_state.focused.valid()) {
        if (scene::TextLabelComponent* label = world.textLabels().find(g_state.focused); label != nullptr) {
            const std::string before = label->text;
            edit(label->text, input.text, input.backspace);
            if (label->text != before) {
                // The same fact a script's own write produces, so a handler on
                // `GetPropertyChangedSignal("Text")` sees typing exactly as it
                // sees an assignment.
                world.changes().push(scene::Change{
                    scene::ChangeKind::PropertyChanged, g_state.focused, {}, world.atoms().intern("Text")});
                if (scene::ScreenGuiComponent* screen = world.screenGuis().find(g_state.focused); screen == nullptr) {
                    for (core::InstanceId current = g_state.focused; current.valid();
                         current = world.parentOf(current)) {
                        if (scene::ScreenGuiComponent* tree = world.screenGuis().find(current); tree != nullptr) {
                            tree->layoutDirty = true;
                            break;
                        }
                    }
                }
            }
        }

        if (input.submit) {
            if (scene::TextInputComponent* field = world.textInputs().find(g_state.focused); field != nullptr)
                field->focused = false;
            fire(world, g_state.focused, "FocusLost");
            g_state.focused = {};
        }
    }

    // A destroyed element stops being hovered, pressed or focused rather than
    // leaving a stale id that would fire an event at a corpse.
    if (!world.alive(g_state.hovered))
        g_state.hovered = {};
    if (!world.alive(g_state.pressedOn))
        g_state.pressedOn = {};
    if (!world.alive(g_state.focused))
        g_state.focused = {};

    result.textInputFocused = g_state.focused.valid();
    return result;
}

} // namespace luaug::ui
