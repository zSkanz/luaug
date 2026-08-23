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
#include "luaug/scene/enum_registry.h"
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
    // M6's screen-space four. Each is a fixed count of floats and nothing else,
    // so they are four kinds rather than one "N floats" kind: the row's label
    // and the drag step differ, and a UDim2 whose four numbers are unlabelled is
    // a widget nobody can use.
    Vector2,
    UDim,
    UDim2,
    Rect,
    // Displayed and never written. Reparenting from the panel is out of M4's
    // scope, and `Parent` is an Instance property -- so an editable reference
    // widget would be the one feature the brief excluded, arriving by accident.
    InstanceRef,
    EnumCombo,
};

[[nodiscard]] EditorKind editorFor(scene::ValueType type) noexcept;

// The enum a property accepts, from the DESCRIPTOR and not from whatever the
// property currently holds. `scene::InvalidEnum` when the property is not an
// enum property, or when this registry does not know the enum it names.
//
// The distinction is the whole point: an editor populates a combo box from a
// property's legal domain, and asking a value what enum it belongs to cannot
// answer for a property that has no value yet, cannot answer without a live
// instance at all, and cannot answer for a class nothing has been created from.
// `PropertyDesc::enumName` is a fact about the class, so this is too.
[[nodiscard]] scene::EnumId enumDomainOf(const scene::EnumRegistry& enums,
                                         const scene::PropertyDesc& descriptor) noexcept;

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

// The properties EVERY instance in `targets` has, in the order the first live
// one carries them.
//
// **The intersection is by name AND by type**, and the second half is not
// pedantry: two unrelated classes may each declare a `Value`, one holding a
// number and the other a string, and one row cannot edit both. It would show
// one member's number, and the write it sent to the other would be refused --
// silently, for anybody not reading the write log. And because the row carries
// the more restrictive of the two descriptors, the widget can be chosen for a
// type the value does not hold, which is a `std::get` that throws. An enum
// property additionally has to name the same enum, because a combo's items are
// the property's legal domain and a row offering another class's items is a row
// that writes a value the class cannot hold.
//
// **The row carries the most restrictive descriptor of the set.** A property
// read-only on any member is drawn read-only for all of them, for the reason
// `editable` exists at all: a field that takes a drag the world then refuses is
// a claim the panel cannot keep, and it would keep it for two of the three
// instances selected -- which is worse than refusing outright, because the two
// that moved look like proof that the third did too.
//
// Order is the FIRST live target's, not the primary's. Ctrl-clicking a fourth
// instance must not reshuffle rows somebody is reading, and the primary is by
// definition the one that just changed.
//
// `out` is cleared first: the panel reuses one buffer across frames.
void collectCommonProperties(const scene::World& world, std::span<const core::InstanceId> targets,
                             std::vector<const scene::PropertyDesc*>& out);

// Whether two values are the same value, for the question below.
//
// `operator==` everywhere except that two NaNs compare unequal and are
// nonetheless not a disagreement anybody typed: a `Number` left at NaN would
// read as mixed across a selection of instances that all hold exactly the same
// thing, and the panel would then refuse to show it.
[[nodiscard]] bool sameValue(const scene::Value& a, const scene::Value& b) noexcept;

// What a selection holds for one property.
enum class SharedState : core::u8
{
    // Nothing readable. Either the set has no live member, or one of them
    // declares the property and the world's getter returned nothing -- and one
    // is enough, because a row that edits three of four instances is a row
    // whose effect nobody can predict.
    Unreadable,
    // The members disagree. `value` is the first live one's, so that a widget
    // has something to step from and a drag has somewhere to start; the panel
    // says the field is mixed rather than implying the number under the cursor
    // is everybody's.
    Mixed,
    Same,
};

struct SharedValue
{
    SharedState state = SharedState::Unreadable;
    scene::Value value;
};

// What `targets` hold for `property`. Dead ids are skipped, so a selection the
// world has partly retired still answers for what is left of it.
[[nodiscard]] SharedValue sharedValue(const scene::World& world, std::span<const core::InstanceId> targets,
                                      core::NameAtom property);

// Whether `Instance.new` would accept this class, and therefore whether the
// editor may offer it.
//
// **Three flags, read where the script binding reads them.** `World::create`
// refuses only an unknown or abstract class and says in its own comment that
// `Service` and `NotCreatable` are "a rule about the script-facing constructor,
// applied in `script`". An editor is a second script-facing constructor, so it
// answers with the same three rather than with a list it would have to keep in
// step -- a class tagged `NotCreatable` tomorrow disappears from the menu with
// no edit here.
[[nodiscard]] bool creatable(const scene::ClassDescriptor& descriptor) noexcept;

// Every class a person may create, in name order.
//
// Name order rather than registration order, because registration order is the
// module layering and nobody browsing a menu is thinking about that. `out` is
// cleared first, and the walk starts at 1: slot zero is the registry's
// placeholder.
void collectCreatableClasses(const scene::World& world, std::vector<scene::ClassId>& out);

// The subtree under `root` in depth-first preorder, which is document order
// (api-design.md §2.2). Sibling order is parenting order and the panel does not
// sort: the tree's order is observable and reproducible, and a sorted view
// would lie about it.
//
// `out` is cleared first, for the same reason as above.
void collectTree(const scene::World& world, core::InstanceId root, std::vector<TreeRow>& out);

// The given instances, in the order the tree holds them, with duplicates and
// anything not under `root` dropped.
//
// **An operation over a set has to be a function of the set.** A batch delete or
// a batch reparent walked in the order somebody happened to ctrl-click would
// produce a different world for the same selection, which is R10's discipline
// applied to an editor. Document order also puts a parent ahead of its own
// child, which is what stops an operation on both from depending on which was
// reached first.
//
// `out` is cleared first.
void orderByTree(const scene::World& world, core::InstanceId root, std::span<const core::InstanceId> ids,
                 std::vector<core::InstanceId>& out);

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
    // --- The selection ------------------------------------------------------
    //
    // **A set, and the primary is the last thing clicked.** One instance was
    // the whole of it until E2, and the shape it grew into is the one every
    // reader already assumed: `selection()` still answers *the* selected
    // instance, which is what a properties grid pointed at one thing and a
    // manipulator anchored to one transform both mean, and `selectionSet()`
    // answers all of them for the callers that act on the whole thing.
    //
    // **Order is click order, not document order.** The primary has to be the
    // one somebody last touched -- a manipulator that anchored to whichever
    // member happened to sort first would jump between objects as the selection
    // grew. A caller that needs determinism over the set (a batch delete, a
    // reparent) sorts by `collectTree` at the moment it acts, where document
    // order is what it means; storing it that way would only make the primary
    // arbitrary.
    //
    // The set lives here rather than in the `Editor` for the reason `editor.h`
    // already gives about the single one: a second copy would be two answers to
    // one question.

    // The primary -- the last instance added -- or an invalid id when nothing
    // is selected.
    [[nodiscard]] core::InstanceId selection() const noexcept
    {
        return selection_.empty() ? core::InstanceId{} : selection_.back();
    }
    [[nodiscard]] std::span<const core::InstanceId> selectionSet() const noexcept { return selection_; }
    [[nodiscard]] usize selectionCount() const noexcept { return selection_.size(); }
    [[nodiscard]] bool isSelected(core::InstanceId id) const noexcept;

    // Replaces the whole selection with one instance. An invalid id clears it,
    // which is what a click on empty space means and what every caller that
    // wrote `select({})` already meant.
    void select(core::InstanceId id) noexcept;
    // Replaces the whole selection with a set, in the order given. Duplicates
    // are dropped and invalid ids are skipped, so a caller assembling a range
    // does not have to.
    void select(std::span<const core::InstanceId> ids);
    // Adds to the selection and makes it primary, or removes it if it was
    // already there -- which is one gesture (ctrl-click) and therefore one
    // function. Removing the primary promotes whatever was added before it.
    void toggle(core::InstanceId id);
    // Adds without removing. Already-selected ids are promoted to primary
    // rather than duplicated.
    void add(core::InstanceId id);
    void clearSelection() noexcept { selection_.clear(); }

    // Drops whatever the world no longer has.
    //
    // **This replaces four hand-written `if (!alive(selection())) select({})`
    // sites, and it is not the same check.** Those compared against ONE id: a
    // stop, an undo, or a delete that removed the selection's PARENT left the
    // set pointing at instances the world had retired, because nothing asked
    // about the subtree. A sweep cannot get that wrong.
    void pruneDead(const scene::World& world);

    // Queues an edit. Never writes: see Decision 15 and `applyPending`.
    void enqueue(core::InstanceId target, core::NameAtom property, scene::Value value);

    [[nodiscard]] usize pendingCount() const noexcept { return pending_.size(); }
    [[nodiscard]] std::span<const PendingWrite> pending() const noexcept { return pending_; }

    // --- Gestures -------------------------------------------------------------
    //
    // **A drag is one edit, and this is what says so.** Undo coalesces on a
    // key, and the key used to be derived from what was in the queue: the
    // target and the property, and only when the frame held exactly ONE write.
    // That is right for a person dragging one slider and wrong for everything
    // E2 adds. A manipulator writes `CFrame` and `Size` together, or writes to
    // three selected parts, so the frame holds two or six writes, so the key is
    // zero, so every frame of the drag records a full world snapshot -- a
    // hundred and twenty undo steps and a hundred and twenty world copies in
    // two seconds.
    //
    // The queue cannot answer this because the answer is not in it. Whoever
    // starts a drag knows a drag started, and that is the only thing that does:
    // ImGui says `IsItemActivated` and `IsItemDeactivatedAfterEdit`, and the
    // gizmo has its own mouse-down and mouse-up. So they say.
    //
    // It also fixes something the old scheme got wrong in the other direction:
    // two separate drags of the same slider shared a key and merged into one
    // undo step, so taking back the second took back the first as well.

    // Opens a gesture and returns its id. Nested calls return the id already
    // open rather than starting a second one -- a widget inside a drag is still
    // the same drag.
    core::u64 beginGesture() noexcept;
    void endGesture() noexcept { gesture_ = 0; }
    [[nodiscard]] core::u64 gesture() const noexcept { return gesture_; }

    // What is waiting, for a caller that has to know BEFORE the drain -- which
    // the undo stack does: it records the world as it was, and "as it was"
    // stops being true the moment `applyPending` runs.
    [[nodiscard]] const PendingWrite& pendingAt(usize index) const noexcept { return pending_[index]; }

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

    // The world was RESTORED to an earlier state of ITSELF -- a stop, an undo,
    // a redo.
    //
    // **This is not the same event and treating it as one costs work.** A
    // `WorldSnapshot` carries generations and the free list precisely so that an
    // `InstanceId` means the same thing after a restore as before it, so
    // everything a panel keyed BY AN ID is still valid: which rows were
    // expanded, what was selected, where a tree was scrolled. An undo that
    // collapsed the whole explorer and dropped the selection takes back more
    // than the edit it was asked to take back.
    //
    // What is invalid is anything keyed by a VALUE. A queued write is an edit
    // this restore has just undone, and replaying it would undo the undo.
    //
    // The caller prunes the selection: what a restore can remove is instances,
    // and `pruneDead` is the sweep that finds them.
    void onWorldRestored() noexcept;

    // How many times the world's contents have been replaced wholesale, by
    // anything at all -- a reload, a scene load, a stop, an undo. What it
    // answers is "is what I cached about the world's VALUES still true", and
    // the frame loop's `render::TransformHistory` is its caller: a restore
    // leaves that history describing where things were in a world that has been
    // put back, which is a part interpolated between two places it is not.
    [[nodiscard]] core::u64 worldGeneration() const noexcept { return worldGeneration_; }

    // How many DIFFERENT worlds this inspector has seen.
    //
    // The other question, and the one a panel holding state keyed by
    // `InstanceId` has to ask -- the explorer's expanded set is one. A new world
    // recycles slot indices from zero, so a new instance landing in a dead one's
    // slot would inherit whether it was expanded, which on a big scene reads as
    // rows opening by themselves. A RESTORED world does not: the snapshot
    // carries generations so that an id keeps its meaning, which is what lets an
    // undo leave the tree exactly as it found it.
    [[nodiscard]] core::u64 worldIdentity() const noexcept { return worldIdentity_; }

    static constexpr usize OutcomeHistory = 8;

private:
    void recordOutcome(const WriteOutcome& outcome);

    std::vector<core::InstanceId> selection_;
    core::u64 gesture_ = 0;
    core::u64 nextGesture_ = 0;
    core::u64 worldGeneration_ = 0;
    core::u64 worldIdentity_ = 0;
    std::vector<PendingWrite> pending_;
    std::vector<WriteOutcome> outcomes_;
};

// The rows between two ids, inclusive, in the order `rows` holds them.
//
// This is shift-click, and it is a free function for the reason every other
// piece of arithmetic in this editor is one: the panel it serves cannot be
// drawn without a window, so anything decided inside the draw callback is
// decided where no test can reach it.
//
// Either id being absent from `rows` -- which a collapsed branch makes
// ordinary -- leaves the selection alone rather than guessing at a range. The
// anchor stays primary, because it is the one somebody is extending FROM.
void selectVisibleRange(Inspector& inspector, std::span<const TreeRow> rows, core::InstanceId anchor,
                        core::InstanceId to);

// The undo key for what is about to be applied.
//
// Zero never coalesces, which is what makes it the safe answer: an unrecognised
// situation becomes its own undo step rather than silently joining the one
// before it.
//
// **An open gesture wins outright**, however many writes it produced and to
// however many instances -- that is the whole point of it. Gesture ids carry
// the top bit so they can never be mistaken for the fallback key below, which
// is built out of an instance index that would need two billion instances to
// reach it.
//
// **The fallback is for every path that has not opted in**, and it is the old
// rule generalised from "exactly one write" to "every write addresses the same
// property of the same instance". A caller that never learned about gestures
// therefore behaves exactly as it did before, which is what keeps this change
// from being a regression in feel somewhere nobody looked.
[[nodiscard]] core::u64 coalesceKeyFor(core::u64 gesture, std::span<const PendingWrite> pending) noexcept;

} // namespace luaug::app
