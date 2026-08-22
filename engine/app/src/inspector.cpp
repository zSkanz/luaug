#include "luaug/app/inspector.h"

#include "luaug/core/math.h"
#include "luaug/scene/enum_registry.h"

#include <algorithm>
#include <cstdio>
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

void Inspector::onWorldChanged() noexcept
{
    ++worldGeneration_;
    selection_ = core::InstanceId{};
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
