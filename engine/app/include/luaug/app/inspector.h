// The DebugShell's explorer and properties panel, minus ImGui (M4 brief,
// Decisions 14, 15 and 16).
//
// This is the half of the panel that can be asserted on: which properties a
// class exposes, which widget a `ValueType` gets, what an edit becomes before
// it reaches the world, and *when* it reaches it. The ImGui half lives in
// `debug_overlay.cpp` and draws exactly what this decides -- pixels of text are
// not something a unit test can hold, and everything else here is.
//
// Three rules shape it, and each is a decision the brief already made:
//
//   * **One generic sweep, no code per class** (Decision 16). `collectProperties`
//     walks the generated descriptor tables, so a class added in a later
//     milestone appears in the inspector with nothing written for it. There is
//     no switch on a class name in this module, and needing one would be a
//     finding about ADR 0017's promise rather than a feature of the panel.
//   * **Writes go through `scene::World::setProperty`, and nowhere else**
//     (Decision 14). A component poked directly would bypass the change queue,
//     so a `Changed` would never enqueue its property-changed fire, a
//     `readOnly` property would be writable from the overlay and not from a
//     script, and the world hash would move with nothing in the log saying why.
//   * **Writes are applied at the FrameStart safe point** (Decision 15). The
//     overlay draws after the sim has ticked and after `extract`, so applying
//     an edit where it is typed would mutate the world after the tick the frame
//     came from -- the mid-frame mutation the scheduler already refuses for hot
//     reload. The panel enqueues; `applyPending` drains one frame later.
//
// Compiled into every profile, shipping included, where the overlay draws
// nothing and the queue is therefore always empty. That is what lets the frame
// loop call `applyPending` unconditionally and stay free of `#ifdef`s
// (ADR 0011), for the same reason `DebugOverlay` keeps its shape there.
//
// R3 does not apply to the text here, for the reason `debug_overlay.h` states:
// the overlay exists for whoever is building the engine, never for a player, so
// its labels are literals rather than catalog keys.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <span>
#include <string>
#include <vector>

namespace luaug::app {

using core::u32;
using core::usize;

// One row of the flattened tree. Depth is what the explorer indents by; the
// order of the rows is document order and is not the panel's to choose.
struct TreeRow
{
    core::InstanceId id;
    u32 depth = 0;
};

// An edit the developer has typed, waiting for the next FrameStart
// (Decision 15). It carries a `Value` rather than a widget's raw bytes because
// that is what `setProperty` takes, so nothing between here and the world has
// to know which editor produced it.
struct PendingWrite
{
    core::InstanceId target;
    core::NameAtom property;
    scene::Value value;
};

// What the drain did with one write. Reported back to the panel so a refusal is
// visible where it was typed: a write that silently does nothing is the failure
// mode `instances.api.luau`'s header names, and it is worse in an inspector
// than anywhere else because the value snaps back with no explanation.
struct WriteOutcome
{
    core::InstanceId target;
    core::NameAtom property;
    scene::World::SetResult result = scene::World::SetResult::UnknownProperty;
};

// One widget per `ValueType`, chosen by `editorFor`. `ReadOnlyText` is the
// deliberate floor rather than a gap: every `ValueType` the registry can hold
// must render *something*, because a type that renders nothing stops being
// inspectable silently, and nobody notices until the class that uses it ships
// (M4 brief, entering risk 6).
enum class EditorKind : core::u8
{
    ReadOnlyText,
    Checkbox,
    Number,
    Text,
    Vector3,
    CFrame,
    Color,
    // Displayed and never written. Reparenting from the panel is out of M4's
    // scope, and `Parent` is an Instance property -- so an editable reference
    // widget would be the one feature the brief excluded, arriving by accident.
    InstanceRef,
    EnumCombo,
};

[[nodiscard]] EditorKind editorFor(scene::ValueType type) noexcept;

// Whether the panel offers a live widget for this property. `readOnly` is
// honoured HERE as well as by the setter, because a field that accepts a drag
// the world then refuses is a UI making a claim it cannot keep.
[[nodiscard]] bool editable(const scene::PropertyDesc& descriptor) noexcept;

// What a property's row says about itself beyond its value: `nullptr` for the
// ordinary case, "(ro)" for read-only, "(stored)" for a property the engine
// keeps faithfully and nothing acts on.
//
// The third one is why this function exists. Every unbacked-behaviour defect
// this project has found was found the same way -- a human changed a value in
// this panel and watched nothing happen -- and in each case the engine was
// behaving exactly as designed while the panel implied otherwise. A widget that
// accepts a value, stores it, reads it back, and changes nothing is
// indistinguishable from a broken one unless it says so.
//
// Read-only wins when a property is somehow both: "you cannot change this" is
// the more useful thing to say, and a read-only property nothing consumes is a
// declaration problem rather than a panel one.
[[nodiscard]] const char* propertyTag(const scene::PropertyDesc& descriptor) noexcept;

// Every property the class has, inherited members first and in slot order --
// the same numbering `ClassRegistry::propertySlot` assigns, so what the panel
// lists and what a subscription addresses are one order.
//
// A class that redeclares an inherited property keeps the inherited position
// and shows the derived descriptor, which is exactly what `findProperty` and
// `propertySlot` already do between them.
//
// `out` is cleared first: the panel reuses one buffer across frames.
void collectProperties(const scene::ClassRegistry& classes, scene::ClassId classId,
                       std::vector<const scene::PropertyDesc*>& out);

// The subtree under `root` in depth-first preorder, which is document order
// (api-design.md §2.2). Sibling order is parenting order and the panel does not
// sort: the tree's order is observable and reproducible, and a sorted view
// would lie about it.
//
// `out` is cleared first, for the same reason as above.
void collectTree(const scene::World& world, core::InstanceId root, std::vector<TreeRow>& out);

// A one-line rendering of any `Value`, for the read-only fields and for the
// outcome log. Needs the world because an Instance reference and an enum item
// are both names the value itself does not carry.
[[nodiscard]] std::string formatValue(const scene::World& world, const scene::Value& value);

// The five outcomes `setProperty` distinguishes, each named differently. The
// panel reports all of them: "nothing happened" and "the engine refused you"
// look identical in a text field otherwise.
[[nodiscard]] const char* setResultLabel(scene::World::SetResult result) noexcept;

// The panel's state between frames: what is selected, what has been typed and
// not yet applied, and what the last few applications did.
class Inspector
{
public:
    [[nodiscard]] core::InstanceId selection() const noexcept { return selection_; }
    void select(core::InstanceId id) noexcept { selection_ = id; }

    // Queues an edit. Never writes: see Decision 15 and `applyPending`.
    void enqueue(core::InstanceId target, core::NameAtom property, scene::Value value);

    [[nodiscard]] usize pendingCount() const noexcept { return pending_.size(); }

    // The FrameStart drain. Every queued write goes through
    // `World::setProperty` -- the same call a script's assignment makes -- and
    // its result is recorded whatever it was.
    //
    // Call at the safe point and nowhere else. A write applied where it is
    // typed lands after the tick the drawn frame came from, which is the
    // mutation ordering hot reload is already forbidden from doing.
    void applyPending(scene::World& world);

    // The last few drains, newest last. Bounded, because this is a panel and
    // not a log: an unbounded history is memory a debug overlay grows forever.
    [[nodiscard]] std::span<const WriteOutcome> outcomes() const noexcept { return outcomes_; }

    // A reload builds a new `World`, and an id minted by the old one may resolve
    // to an unrelated instance in the new one rather than to nothing -- slot
    // indices are reused from zero. So both the selection and anything still
    // queued are dropped rather than replayed against a world that never saw
    // them.
    void onWorldChanged() noexcept;

    static constexpr usize OutcomeHistory = 8;

private:
    void recordOutcome(const WriteOutcome& outcome);

    core::InstanceId selection_;
    std::vector<PendingWrite> pending_;
    std::vector<WriteOutcome> outcomes_;
};

} // namespace luaug::app
