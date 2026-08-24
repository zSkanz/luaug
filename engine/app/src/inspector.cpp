#include "luaug/app/inspector.h"

#include "luaug/core/math.h"
#include "luaug/scene/enum_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>
#include <variant>

namespace luaug::app {
namespace {

using core::f64;

// Wide enough for the longest thing formatted below -- a CFrame's position.
// Truncation here would be a display bug rather than a memory one, but a
// silently shortened number in an inspector is a lie about the world's state.
constexpr usize FormatBufferSize = 160;

// One `operator()` per alternative of `scene::Value`, deliberately, rather than
// a generic lambda with a fallback. Appending an alternative to the variant
// then becomes a compile error here instead of a `ValueType` that quietly
// renders as an empty field -- entering risk 6's exact failure mode, moved from
// runtime to the build.
//
// Every format string is a literal at its call site: `-Wformat=2` rejects a
// format that arrived through a variable, and it is right to.
struct ValueFormatter
{
    const scene::World& world;

    [[nodiscard]] std::string operator()(std::monostate) const { return "nil"; }

    [[nodiscard]] std::string operator()(bool value) const { return value ? "true" : "false"; }

    [[nodiscard]] std::string operator()(f64 value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const std::string& value) const { return "\"" + value + "\""; }

    [[nodiscard]] std::string operator()(const core::Vec3& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", static_cast<f64>(value.x), static_cast<f64>(value.y),
                      static_cast<f64>(value.z));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::CFrameD& value) const
    {
        // Position only. The rotation is nine numbers and no inspector row is
        // wide enough for them; the properties panel draws the basis beneath
        // the position, where there is space.
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "pos %.3f, %.3f, %.3f", value.position.x, value.position.y,
                      value.position.z);
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::Color3& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "rgb %.3f, %.3f, %.3f", static_cast<f64>(value.r),
                      static_cast<f64>(value.g), static_cast<f64>(value.b));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::Vec2& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f", static_cast<f64>(value.x), static_cast<f64>(value.y));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::UDim& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "%.3f, %.0f", static_cast<f64>(value.scale),
                      static_cast<f64>(value.offset));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::UDim2& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "{%.3f, %.0f}, {%.3f, %.0f}", static_cast<f64>(value.x.scale),
                      static_cast<f64>(value.x.offset), static_cast<f64>(value.y.scale),
                      static_cast<f64>(value.y.offset));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(const core::Rect& value) const
    {
        char buffer[FormatBufferSize]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f, %.1f -> %.1f, %.1f", static_cast<f64>(value.min.x),
                      static_cast<f64>(value.min.y), static_cast<f64>(value.max.x), static_cast<f64>(value.max.y));
        return std::string(buffer);
    }

    [[nodiscard]] std::string operator()(core::InstanceId value) const
    {
        if (!value.valid())
            return "nil";
        // A reference the world has already let go of. Said out loud rather
        // than drawn as a name, because a dangling reference is a fact about
        // the world and the panel exists to show facts about the world.
        if (!world.alive(value))
            return "<stale>";

        const std::string_view instanceName = world.atoms().text(world.name(value));
        const scene::ClassDescriptor* classDescriptor = world.classes().find(world.classOf(value));
        const std::string_view className =
            classDescriptor != nullptr ? world.atoms().text(classDescriptor->name) : std::string_view("?");
        return std::string(instanceName) + " (" + std::string(className) + ")";
    }

    [[nodiscard]] std::string operator()(const scene::EnumValue& value) const
    {
        char buffer[FormatBufferSize]{};

        const scene::EnumDescriptor* enumDescriptor = world.enums().find(value.enumId);
        if (enumDescriptor == nullptr) {
            std::snprintf(buffer, sizeof(buffer), "<enum %u>.%d", static_cast<unsigned>(value.enumId), value.value);
            return std::string(buffer);
        }

        const std::string_view enumName = world.atoms().text(enumDescriptor->name);
        const scene::EnumItemDesc* item = world.enums().findValue(value.enumId, value.value);
        if (item != nullptr)
            return "Enum." + std::string(enumName) + "." + std::string(world.atoms().text(item->name));

        // A stored number no item carries. Shown rather than hidden: it is
        // exactly the state a snapshot from an older enum leaves behind.
        std::snprintf(buffer, sizeof(buffer), "%d", value.value);
        return "Enum." + std::string(enumName) + ".<" + buffer + ">";
    }
};

} // namespace

EditorKind editorFor(scene::ValueType type) noexcept
{
    switch (type) {
    case scene::ValueType::Nil:
        break;
    case scene::ValueType::Bool:
        return EditorKind::Checkbox;
    case scene::ValueType::Number:
        return EditorKind::Number;
    case scene::ValueType::String:
        return EditorKind::Text;
    case scene::ValueType::Vector3:
        return EditorKind::Vector3;
    case scene::ValueType::CFrame:
        return EditorKind::CFrame;
    case scene::ValueType::Color3:
        return EditorKind::Color;
    case scene::ValueType::Instance:
        return EditorKind::InstanceRef;
    case scene::ValueType::EnumItem:
        return EditorKind::EnumCombo;
    case scene::ValueType::Vector2:
        return EditorKind::Vector2;
    case scene::ValueType::UDim:
        return EditorKind::UDim;
    case scene::ValueType::UDim2:
        return EditorKind::UDim2;
    case scene::ValueType::Rect:
        return EditorKind::Rect;
    }

    // `Nil` is a property holding nothing, and so is anything the switch above
    // stops naming. Both land on a disabled field rather than on no field at
    // all: a `ValueType` that renders nothing stops being inspectable without
    // anyone finding out (entering risk 6).
    return EditorKind::ReadOnlyText;
}

EditorKind editorFor(const scene::PropertyDesc& descriptor) noexcept
{
    // A `Content` is a string, and the descriptor is the only thing that knows
    // it is one: the value cannot say, because a URI and a name are the same
    // bytes. `contentKind` is set by the IDL for exactly these properties.
    if (descriptor.type == scene::ValueType::String && descriptor.contentKind.valid())
        return EditorKind::Content;
    return editorFor(descriptor.type);
}

bool editable(const scene::PropertyDesc& descriptor) noexcept
{
    if (descriptor.readOnly || descriptor.set == nullptr)
        return false;

    const EditorKind kind = editorFor(descriptor.type);
    return kind != EditorKind::ReadOnlyText && kind != EditorKind::InstanceRef;
}

scene::EnumId enumDomainOf(const scene::EnumRegistry& enums, const scene::PropertyDesc& descriptor) noexcept
{
    if (descriptor.type != scene::ValueType::EnumItem)
        return scene::InvalidEnum;
    // `findId` on an empty atom answers `InvalidEnum` already, so a hand-built
    // descriptor that names no enum falls out here rather than needing a case.
    return enums.findId(descriptor.enumName);
}

const char* propertyTag(const scene::PropertyDesc& descriptor) noexcept
{
    if (descriptor.readOnly)
        return "(ro)";
    if (descriptor.inert)
        return "(stored)";
    return nullptr;
}

void collectProperties(const scene::ClassRegistry& classes, scene::ClassId classId,
                       std::vector<const scene::PropertyDesc*>& out)
{
    out.clear();

    // The descriptor's `properties` span holds only what the class declares, so
    // the sweep is the ancestry walk. Collected leaf-first and replayed in
    // reverse, because the numbering that matters -- `propertySlot`'s -- puts
    // the root's members first.
    std::vector<scene::ClassId> ancestry;
    for (scene::ClassId id = classId; id != scene::InvalidClass;) {
        const scene::ClassDescriptor* descriptor = classes.find(id);
        if (descriptor == nullptr)
            break;
        ancestry.push_back(id);
        id = descriptor->super;
    }

    for (auto step = ancestry.rbegin(); step != ancestry.rend(); ++step) {
        const scene::ClassDescriptor* descriptor = classes.find(*step);
        for (const scene::PropertyDesc& property : descriptor->properties) {
            // A class that redeclares an inherited property keeps the inherited
            // position and shows the derived descriptor -- the same rule the
            // registry follows for the slot, which a subscription made through
            // the base depends on.
            const auto shadowed =
                std::find_if(out.begin(), out.end(), [&property](const scene::PropertyDesc* candidate) {
                    return candidate->name == property.name;
                });
            if (shadowed != out.end())
                *shadowed = &property;
            else
                out.push_back(&property);
        }
    }
}

bool sameValue(const scene::Value& a, const scene::Value& b) noexcept
{
    const f64* left = std::get_if<f64>(&a);
    const f64* right = std::get_if<f64>(&b);
    if (left != nullptr && right != nullptr && std::isnan(*left) && std::isnan(*right))
        return true;
    return a == b;
}

void collectCommonProperties(const scene::World& world, std::span<const core::InstanceId> targets,
                             std::vector<const scene::PropertyDesc*>& out)
{
    out.clear();

    usize first = 0;
    while (first < targets.size() && !world.alive(targets[first]))
        ++first;
    if (first >= targets.size())
        return;

    collectProperties(world.classes(), world.classOf(targets[first]), out);

    // A shift-range over five hundred parts is one class five hundred times,
    // and intersecting a set with itself is the whole of the work. The memo is
    // the immediately preceding class rather than a set of every class seen,
    // because a run of one class is what a real selection is and the answer is
    // correct either way -- intersection is idempotent, so skipping a repeat
    // cannot change the result, only the time it takes to reach it.
    scene::ClassId previous = world.classOf(targets[first]);

    std::vector<const scene::PropertyDesc*> other;
    for (usize i = first + 1; i < targets.size() && !out.empty(); ++i) {
        if (!world.alive(targets[i]))
            continue;
        const scene::ClassId classId = world.classOf(targets[i]);
        if (classId == previous)
            continue;
        previous = classId;

        collectProperties(world.classes(), classId, other);

        usize write = 0;
        for (const scene::PropertyDesc* mine : out) {
            const auto match = std::find_if(other.begin(), other.end(), [mine](const scene::PropertyDesc* candidate) {
                return candidate->name == mine->name && candidate->type == mine->type &&
                       candidate->enumName == mine->enumName;
            });
            if (match == other.end())
                continue;
            out[write++] = (*match)->readOnly && !mine->readOnly ? *match : mine;
        }
        out.resize(write);
    }
}

SharedValue sharedValue(const scene::World& world, std::span<const core::InstanceId> targets, core::NameAtom property)
{
    SharedValue shared;
    bool seen = false;
    for (const core::InstanceId id : targets) {
        if (!world.alive(id))
            continue;

        const std::optional<scene::Value> value = world.getProperty(id, property);
        if (!value.has_value())
            return SharedValue{};

        if (!seen) {
            shared.state = SharedState::Same;
            shared.value = *value;
            seen = true;
        }
        else if (!sameValue(shared.value, *value)) {
            shared.state = SharedState::Mixed;
        }
    }
    return shared;
}

void collectAncestors(const scene::World& world, core::InstanceId id, core::InstanceId root,
                      std::vector<core::InstanceId>& out)
{
    out.clear();
    if (!world.alive(id))
        return;

    for (core::InstanceId walk = world.parentOf(id); walk.valid() && world.alive(walk); walk = world.parentOf(walk)) {
        out.push_back(walk);
        if (walk == root)
            break;
    }
}

void orderByTree(const scene::World& world, core::InstanceId root, std::span<const core::InstanceId> ids,
                 std::vector<core::InstanceId>& out)
{
    out.clear();
    if (ids.empty())
        return;

    // One walk of the tree rather than a sort with a comparator that would have
    // to answer "which of these two comes first" by walking it anyway.
    static thread_local std::vector<TreeRow> rows;
    collectTree(world, root, rows);
    out.reserve(ids.size());
    for (const TreeRow& row : rows) {
        if (std::find(ids.begin(), ids.end(), row.id) != ids.end() &&
            std::find(out.begin(), out.end(), row.id) == out.end()) {
            out.push_back(row.id);
        }
    }
}

bool creatable(const scene::ClassDescriptor& descriptor) noexcept
{
    return !scene::hasFlag(descriptor.flags, scene::ClassFlags::Abstract) &&
           !scene::hasFlag(descriptor.flags, scene::ClassFlags::Service) &&
           !scene::hasFlag(descriptor.flags, scene::ClassFlags::NotCreatable);
}

void collectCreatableClasses(const scene::World& world, std::vector<scene::ClassId>& out)
{
    out.clear();
    const scene::ClassRegistry& classes = world.classes();
    // From 1: slot zero is the registry's placeholder and never a class.
    for (scene::ClassId id = 1; id < static_cast<scene::ClassId>(classes.classCount()); ++id) {
        const scene::ClassDescriptor* descriptor = classes.find(id);
        if (descriptor != nullptr && creatable(*descriptor))
            out.push_back(id);
    }

    std::sort(out.begin(), out.end(), [&world, &classes](scene::ClassId a, scene::ClassId b) {
        return world.atoms().text(classes.find(a)->name) < world.atoms().text(classes.find(b)->name);
    });
}

void collectTree(const scene::World& world, core::InstanceId root, std::vector<TreeRow>& out)
{
    out.clear();
    if (!root.valid() || !world.alive(root))
        return;

    std::vector<TreeRow> stack{TreeRow{root, 0}};
    std::vector<core::InstanceId> children;

    while (!stack.empty()) {
        const TreeRow row = stack.back();
        stack.pop_back();
        out.push_back(row);

        // Children are pushed in reverse so the first one pops first. That is
        // what makes this preorder, and preorder over `collectChildren` is
        // parenting order at every level -- the order `GetDescendants`
        // promises, and the one the panel is forbidden from improving on.
        children.clear();
        world.collectChildren(row.id, children);
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            stack.push_back(TreeRow{*child, row.depth + 1});
    }
}

void collectVisibleTree(const scene::World& world, core::InstanceId root, bool includeRoot,
                        const std::function<TreeVisit(const TreeRow&)>& visit, std::vector<TreeRow>& out)
{
    out.clear();
    if (!root.valid() || !world.alive(root) || !visit)
        return;

    // A stack of candidates rather than of subtrees, so the answer for a node is
    // asked once and its children are only ever pushed after an `Expanded`. That
    // is the whole difference from `collectTree`: a closed or hidden subtree is
    // never touched, so nothing here is a function of how big the world is.
    std::vector<TreeRow> stack{TreeRow{root, 0}};
    std::vector<core::InstanceId> children;

    while (!stack.empty()) {
        const TreeRow row = stack.back();
        stack.pop_back();

        const TreeVisit answer = visit(row);
        if (answer == TreeVisit::Skip)
            continue;
        if (row.depth > 0 || includeRoot)
            out.push_back(row);
        if (answer != TreeVisit::Expanded)
            continue;

        // Reversed for the reason `collectTree` reverses: the first child has to
        // pop first, which is what makes this preorder rather than a mirror of
        // it. Sibling order is parenting order and the panel does not sort.
        children.clear();
        world.collectChildren(row.id, children);
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            stack.push_back(TreeRow{*child, row.depth + 1});
    }
}

std::string formatValue(const scene::World& world, const scene::Value& value)
{
    return std::visit(ValueFormatter{world}, value);
}

const char* setResultLabel(scene::World::SetResult result) noexcept
{
    switch (result) {
    case scene::World::SetResult::Changed:
        return "changed";
    case scene::World::SetResult::Unchanged:
        return "unchanged";
    case scene::World::SetResult::UnknownProperty:
        return "unknown property";
    case scene::World::SetResult::ReadOnly:
        return "read-only";
    case scene::World::SetResult::InvalidValue:
        return "invalid value";
    }
    return "?";
}

void selectVisibleRange(Inspector& inspector, std::span<const TreeRow> rows, core::InstanceId anchor,
                        core::InstanceId to)
{
    const auto find = [rows](core::InstanceId id) -> usize {
        for (usize i = 0; i < rows.size(); ++i) {
            if (rows[i].id == id)
                return i;
        }
        return rows.size();
    };

    const usize from = find(anchor);
    const usize until = find(to);
    if (from == rows.size() || until == rows.size())
        return;

    const usize first = from < until ? from : until;
    const usize last = from < until ? until : from;

    std::vector<core::InstanceId> range;
    range.reserve(last - first + 1);
    for (usize i = first; i <= last; ++i) {
        if (rows[i].id != anchor)
            range.push_back(rows[i].id);
    }
    // Last, so it is the primary: the anchor is what the next shift-click
    // extends from, and a range that promoted its far end would walk the anchor
    // along with every click.
    range.push_back(anchor);
    inspector.select(range);
}

bool Inspector::isSelected(core::InstanceId id) const noexcept
{
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void Inspector::select(core::InstanceId id) noexcept
{
    selection_.clear();
    if (id.valid())
        selection_.push_back(id);
}

void Inspector::select(std::span<const core::InstanceId> ids)
{
    selection_.clear();
    for (const core::InstanceId id : ids)
        add(id);
}

void Inspector::add(core::InstanceId id)
{
    if (!id.valid())
        return;
    // Promoted rather than duplicated. Clicking something already selected has
    // to make it the primary -- that is how somebody chooses which member a
    // manipulator anchors to without losing the rest of the selection.
    std::erase(selection_, id);
    selection_.push_back(id);
}

void Inspector::toggle(core::InstanceId id)
{
    if (!id.valid())
        return;
    if (const auto it = std::find(selection_.begin(), selection_.end(), id); it != selection_.end()) {
        selection_.erase(it);
        return;
    }
    selection_.push_back(id);
}

void Inspector::pruneDead(const scene::World& world)
{
    std::erase_if(selection_, [&world](const core::InstanceId id) { return !world.alive(id); });
}

core::u64 Inspector::beginGesture() noexcept
{
    if (gesture_ != 0)
        return gesture_;
    // The top bit, so a gesture id and the property-derived fallback key can
    // never be the same number -- see `coalesceKeyFor`.
    gesture_ = (core::u64{1} << 63) | ++nextGesture_;
    return gesture_;
}

core::u64 coalesceKeyFor(core::u64 gesture, std::span<const PendingWrite> pending) noexcept
{
    if (gesture != 0)
        return gesture;
    if (pending.empty())
        return 0;

    const PendingWrite& first = pending.front();
    for (const PendingWrite& write : pending) {
        if (!(write.target == first.target) || !(write.property == first.property))
            return 0;
    }
    return (static_cast<core::u64>(first.target.index) << 32) | first.property.id;
}

void Inspector::enqueue(core::InstanceId target, core::NameAtom property, scene::Value value)
{
    pending_.push_back(PendingWrite{target, property, std::move(value)});
}

void Inspector::applyPending(scene::World& world)
{
    for (const PendingWrite& write : pending_) {
        // Decision 14, and the whole of it: the same call a script's assignment
        // makes, so the change queue, the `readOnly` refusal and the world hash
        // all see an overlay edit exactly as they see a scripted one.
        const scene::World::SetResult result = world.setProperty(write.target, write.property, write.value);
        recordOutcome(WriteOutcome{write.target, write.property, result});
    }
    pending_.clear();
}

void Inspector::onWorldRestored() noexcept
{
    // The values moved; the ids did not. So the value-keyed half goes and the
    // id-keyed half stays -- see the header, and D071 for what conflating the
    // two cost.
    ++worldGeneration_;
    gesture_ = 0;
    pending_.clear();
}

void Inspector::onWorldChanged() noexcept
{
    ++worldGeneration_;
    ++worldIdentity_;
    selection_.clear();
    // A different world recycles slot indices from zero, so an id minted by the
    // old one names an unrelated instance in the new one -- and revealing that
    // would open a branch nobody asked about.
    reveal_ = core::InstanceId{};
    // A gesture is a drag over instances this world no longer has. Leaving it
    // open would coalesce the next unrelated edit into whatever came before the
    // reload.
    gesture_ = 0;
    pending_.clear();
    outcomes_.clear();
}

void Inspector::recordOutcome(const WriteOutcome& outcome)
{
    outcomes_.push_back(outcome);
    // One at a time in, one at a time out: a panel is not a log, and an
    // unbounded history is memory a debug overlay grows for as long as it runs.
    if (outcomes_.size() > OutcomeHistory)
        outcomes_.erase(outcomes_.begin());
}

} // namespace luaug::app
