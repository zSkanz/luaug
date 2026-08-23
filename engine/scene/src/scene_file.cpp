#include <luaug/core/i18n.h>
#include <luaug/core/json.h>
#include <luaug/core/json_writer.h>
#include <luaug/scene/class_registry.h>
#include <luaug/scene/components.h>
#include <luaug/scene/enum_registry.h>
#include <luaug/scene/scene_file.h>
#include <luaug/scene/world.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace luaug::scene {
namespace {
using core::JsonValue;
using core::JsonWriter;

constexpr std::string_view kFormat = "luaug-scene";
constexpr core::i64 kVersion = 1;

// Written as fields of their own, so writing them again as properties would be
// two spellings of one fact -- and `Parent` in particular would fight the
// nesting that already expresses it.
[[nodiscard]] bool isStructuralProperty(std::string_view name) noexcept
{
    return name == "Name" || name == "Parent" || name == "ClassName";
}

[[nodiscard]] core::InstanceId workspaceOf(const World& world) noexcept
{
    core::InstanceId found;
    world.workspaces().forEach([&](core::InstanceId id, const WorkspaceComponent&) {
        // One per world by construction. Taking the first rather than asserting,
        // because a world with none is a world booting, not a defect.
        if (!found.valid())
            found = id;
    });
    return found;
}

// --- writing ---------------------------------------------------------------

void writeValue(JsonWriter& out, const World& world, const Value& value,
                const std::unordered_map<core::u32, std::string>& paths, SceneIoReport& report)
{
    switch (valueType(value)) {
    case ValueType::Nil:
        out.nullValue();
        break;
    case ValueType::Bool:
        out.value(std::get<bool>(value));
        break;
    case ValueType::Number:
        out.value(std::get<core::f64>(value));
        break;
    case ValueType::String:
        out.value(std::get<std::string>(value));
        break;
    case ValueType::Vector3: {
        const core::Vec3 v = std::get<core::Vec3>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(v.x));
        out.value(static_cast<core::f64>(v.y));
        out.value(static_cast<core::f64>(v.z));
        out.endArray();
        break;
    }
    case ValueType::CFrame: {
        // Position first, then the rotation's nine, in the column order `Mat3`
        // stores them. Twelve numbers rather than a position and three Euler
        // angles: Euler angles are a lossy round trip through a branch choice,
        // and a scene that moved a part by a thousandth of a degree every save
        // would make every diff noise.
        const core::CFrameD cf = std::get<core::CFrameD>(value);
        out.beginArray();
        out.value(cf.position.x);
        out.value(cf.position.y);
        out.value(cf.position.z);
        for (core::i32 column = 0; column < 3; ++column)
            for (core::i32 row = 0; row < 3; ++row)
                out.value(static_cast<core::f64>(cf.rotation.m[column][row]));
        out.endArray();
        break;
    }
    case ValueType::Color3: {
        const core::Color3 c = std::get<core::Color3>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(c.r));
        out.value(static_cast<core::f64>(c.g));
        out.value(static_cast<core::f64>(c.b));
        out.endArray();
        break;
    }
    case ValueType::Instance: {
        const core::InstanceId target = std::get<core::InstanceId>(value);
        if (!target.valid()) {
            out.nullValue();
            break;
        }
        const auto found = paths.find(target.index);
        if (found == paths.end()) {
            // Outside the scene: a service, a streamed chunk, something a script
            // made. Named as nothing rather than as the wrong thing.
            ++report.droppedReferences;
            out.nullValue();
            break;
        }
        out.value(found->second);
        break;
    }
    case ValueType::EnumItem: {
        const EnumValue item = std::get<EnumValue>(value);
        const EnumDescriptor* descriptor = world.enums().find(static_cast<EnumId>(item.enumId));
        const EnumItemDesc* named =
            descriptor != nullptr ? world.enums().findValue(static_cast<EnumId>(item.enumId), item.value) : nullptr;
        // By NAME, for the reason a class is: an enum's numeric value is a
        // contract, but the name is what a person reads in a diff and what
        // survives somebody renumbering.
        if (named != nullptr)
            out.value(world.atoms().text(named->name));
        else
            out.value(static_cast<core::i64>(item.value));
        break;
    }
    case ValueType::Vector2: {
        const core::Vec2 v = std::get<core::Vec2>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(v.x));
        out.value(static_cast<core::f64>(v.y));
        out.endArray();
        break;
    }
    case ValueType::UDim: {
        const core::UDim u = std::get<core::UDim>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(u.scale));
        out.value(static_cast<core::f64>(u.offset));
        out.endArray();
        break;
    }
    case ValueType::UDim2: {
        const core::UDim2 u = std::get<core::UDim2>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(u.x.scale));
        out.value(static_cast<core::f64>(u.x.offset));
        out.value(static_cast<core::f64>(u.y.scale));
        out.value(static_cast<core::f64>(u.y.offset));
        out.endArray();
        break;
    }
    case ValueType::Rect: {
        const core::Rect r = std::get<core::Rect>(value);
        out.beginArray();
        out.value(static_cast<core::f64>(r.min.x));
        out.value(static_cast<core::f64>(r.min.y));
        out.value(static_cast<core::f64>(r.max.x));
        out.value(static_cast<core::f64>(r.max.y));
        out.endArray();
        break;
    }
    }
}

// Every instance's path, built before anything is written, because a property
// on the first instance may reference the last.
void collectPaths(const World& world, core::InstanceId id, const std::string& prefix,
                  std::unordered_map<core::u32, std::string>& out)
{
    const std::string path = prefix.empty() ? std::string(world.atoms().text(world.name(id)))
                                            : prefix + "." + std::string(world.atoms().text(world.name(id)));
    out.emplace(id.index, path);
    for (core::InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child)) {
        // Not written, so not nameable. A path collected for something the file
        // will not contain is a reference that resolves to nothing on load,
        // which is worse than the null the dropped-reference count reports.
        if (world.generated(child))
            continue;
        collectPaths(world, child, path, out);
    }
}

// The one property a stamped instance keeps as its own (ADR 0049): where it is.
// A class with no `CFrame` simply has no placement to preserve, which is not a
// case that needs handling.
constexpr std::string_view kTransformProperty = "CFrame";

// `expandStamped` is the instance whose own stamp mark is IGNORED, and there is
// exactly one situation with one: writing the stamp FILE, whose root is an
// instance of the stamp it is being written from. Every other stamped instance
// collapses to its mark.
void writeInstance(JsonWriter& out, const World& world, core::InstanceId id,
                   const std::unordered_map<core::u32, std::string>& paths, SceneIoReport& report,
                   core::InstanceId expandStamped = core::InstanceId{})
{
    out.beginObject();

    const ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    out.field("class", descriptor != nullptr ? world.atoms().text(descriptor->name) : std::string_view{});
    out.field("name", world.atoms().text(world.name(id)));
    ++report.instances;

    // **A stamped instance is written as its MARK, its name and where it is, and
    // nothing else** (ADR 0049). Its children belong to the stamp file; writing
    // them again would be the full copy that makes the whole idea worthless, and
    // it would go stale the moment the stamp changed.
    //
    // There is nothing else to write, and that falls out of the break rule
    // rather than being a second decision: if every other edit breaks the mark,
    // a marked instance cannot have any other override.
    const core::NameAtom stamp = world.stampOf(id);
    if (stamp.valid() && id != expandStamped) {
        out.field("stamp", world.atoms().text(stamp));
        const PropertyDesc* transform =
            world.classes().findProperty(world.classOf(id), world.atoms().lookup(kTransformProperty));
        if (transform != nullptr && transform->get != nullptr) {
            if (const std::optional<Value> value = transform->get(world, id); value.has_value()) {
                out.key("properties");
                out.beginObject();
                out.key(kTransformProperty);
                writeValue(out, world, *value, paths, report);
                out.endObject();
                ++report.properties;
            }
        }
        out.endObject();
        return;
    }

    // The same ancestry walk the world hash makes, and in the same order, so a
    // property redeclared by a subclass is written once and by the subclass.
    bool anyProperty = false;
    for (const ClassDescriptor* current = descriptor; current != nullptr;
         current = world.classes().find(current->super)) {
        for (const PropertyDesc& property : current->properties) {
            // A property with no setter cannot be applied on load, so writing it
            // would be a field that is only ever read by a person. `readOnly` is
            // the same statement from the other side.
            if (property.get == nullptr || property.set == nullptr || property.readOnly)
                continue;
            const std::string_view name = world.atoms().text(property.name);
            if (isStructuralProperty(name))
                continue;

            const std::optional<Value> value = property.get(world, id);
            if (!value.has_value())
                continue;

            if (!anyProperty) {
                out.key("properties");
                out.beginObject();
                anyProperty = true;
            }
            out.key(name);
            writeValue(out, world, *value, paths, report);
            ++report.properties;
        }
    }
    if (anyProperty)
        out.endObject();

    AttributeMap attributes;
    world.collectAttributes(id, attributes);
    if (!attributes.empty()) {
        out.key("attributes");
        out.beginObject();
        // Insertion-ordered by construction, so this is stable without sorting
        // -- the same property the world hash relies on.
        for (const auto& entry : attributes) {
            out.key(world.atoms().text(entry.first));
            writeValue(out, world, entry.second, paths, report);
        }
        out.endObject();
    }

    TagSet tags;
    world.collectTags(id, tags);
    if (!tags.empty()) {
        out.key("tags");
        out.beginArray();
        for (const core::NameAtom tag : tags)
            out.value(world.atoms().text(tag));
        out.endArray();
    }

    if (world.firstChild(id).valid()) {
        out.key("children");
        out.beginArray();
        // Sibling order, which is observable through `GetChildren` and is
        // therefore part of what a scene has to reproduce.
        for (core::InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child)) {
            // A system made it, so nobody wrote it down and a scene does not
            // record it -- and the whole subtree goes with it, because the parts
            // inside a streamed chunk were not separately authored either.
            if (world.generated(child))
                continue;
            writeInstance(out, world, child, paths, report);
        }
        out.endArray();
    }

    out.endObject();
}

// --- reading ---------------------------------------------------------------

// A reference cannot be resolved while the tree is being built, because it may
// name something that does not exist yet. Collected and applied at the end.
struct PendingReference
{
    core::InstanceId owner;
    core::NameAtom property;
    std::string path;
    bool isAttribute = false;
};

[[nodiscard]] std::optional<Value> readValue(ValueType expected, const JsonValue& json,
                                             std::vector<PendingReference>& pending, core::InstanceId owner,
                                             core::NameAtom property, bool isAttribute)
{
    const auto number = [](const JsonValue& v, core::usize index) -> core::f64 { return v.at(index).asNumber(); };
    const auto f32At = [&number](const JsonValue& v, core::usize index) -> core::f32 {
        return static_cast<core::f32>(number(v, index));
    };

    switch (expected) {
    case ValueType::Nil:
        return Value{};
    case ValueType::Bool:
        return Value{json.asBool()};
    case ValueType::Number:
        return Value{json.asNumber()};
    case ValueType::String:
        return Value{std::string(json.asString())};
    case ValueType::Vector3:
        if (json.size() < 3)
            return std::nullopt;
        return Value{core::Vec3{f32At(json, 0), f32At(json, 1), f32At(json, 2)}};
    case ValueType::CFrame: {
        if (json.size() < 12)
            return std::nullopt;
        core::CFrameD cf;
        cf.position = {number(json, 0), number(json, 1), number(json, 2)};
        core::usize at = 3;
        for (core::i32 column = 0; column < 3; ++column)
            for (core::i32 row = 0; row < 3; ++row)
                cf.rotation.m[column][row] = f32At(json, at++);
        return Value{cf};
    }
    case ValueType::Color3:
        if (json.size() < 3)
            return std::nullopt;
        return Value{core::Color3{f32At(json, 0), f32At(json, 1), f32At(json, 2)}};
    case ValueType::Instance:
        if (json.isNull())
            return Value{core::InstanceId{}};
        // Deferred: the target may be written later in the file.
        pending.push_back(PendingReference{owner, property, std::string(json.asString()), isAttribute});
        return Value{core::InstanceId{}};
    case ValueType::EnumItem:
        // Resolved by the caller, which is the only place that knows WHICH enum
        // the property accepts.
        return std::nullopt;
    case ValueType::Vector2:
        if (json.size() < 2)
            return std::nullopt;
        return Value{core::Vec2{f32At(json, 0), f32At(json, 1)}};
    case ValueType::UDim:
        if (json.size() < 2)
            return std::nullopt;
        return Value{core::UDim{f32At(json, 0), f32At(json, 1)}};
    case ValueType::UDim2:
        if (json.size() < 4)
            return std::nullopt;
        return Value{
            core::UDim2{core::UDim{f32At(json, 0), f32At(json, 1)}, core::UDim{f32At(json, 2), f32At(json, 3)}}};
    case ValueType::Rect:
        if (json.size() < 4)
            return std::nullopt;
        return Value{
            core::Rect{core::Vec2{f32At(json, 0), f32At(json, 1)}, core::Vec2{f32At(json, 2), f32At(json, 3)}}};
    }
    return std::nullopt;
}

core::InstanceId readInstance(World& world, core::InstanceId parent, const JsonValue& json,
                              std::vector<PendingReference>& pending, SceneIoReport& report,
                              const StampSource* stamps = nullptr, int depth = 0);

// How deep a stamp may name another stamp before this stops asking.
//
// A stamp of a stamp is refused at authoring time (ADR 0049), so the only way
// to reach this is a hand-edited file -- including one that names ITSELF, which
// without a limit is an infinite tree and a dead process. Four rather than one,
// because refusing a legal-looking file outright is a worse answer than
// refusing an absurd one.
constexpr int kMaxStampDepth = 4;

// Reads the stamp `name` through the caller's source and builds it under
// `parent`, marked. An invalid id means the source had nothing, the text was
// not a stamp, or the class it names is one this build does not have.
[[nodiscard]] core::InstanceId placeStamp(World& world, core::InstanceId parent, std::string_view name,
                                          SceneIoReport& report, const StampSource* stamps, int depth);

// Everything an instance carries that is not its identity or its children.
//
// Split out because the scene's ROOT is applied to the `Workspace` that already
// exists rather than created -- and its own properties are as much a part of the
// world as its children's are. `CurrentCamera` is the one that proves it: a
// scene that restored every part and not the camera would load into a world
// nothing can see.
void applyNode(World& world, core::InstanceId id, const JsonValue& json, std::vector<PendingReference>& pending,
               SceneIoReport& report)
{
    const ClassId classId = world.classOf(id);
    if (const JsonValue properties = json["properties"]; properties.type() == core::JsonType::Object) {
        for (core::usize index = 0; index < properties.size(); ++index) {
            const std::string_view name = properties.keyAt(index);
            const core::NameAtom atom = world.atoms().intern(name);
            const PropertyDesc* property = world.classes().findProperty(classId, atom);
            if (property == nullptr || property->set == nullptr) {
                // A scene written by a newer build should still open here, minus
                // what this one cannot express. Counted, never fatal.
                ++report.refusedProperties;
                continue;
            }

            const JsonValue json2 = properties[name];
            std::optional<Value> value;
            if (property->type == ValueType::EnumItem) {
                // The domain comes from the descriptor, which is what
                // `PropertyDesc::enumName` was added for -- resolving it from
                // the current value would need a value to already be there.
                const EnumId enumId = world.enums().findId(property->enumName);
                if (const EnumItemDesc* item =
                        enumId != InvalidEnum ? world.enums().findItem(enumId, world.atoms().intern(json2.asString()))
                                              : nullptr;
                    item != nullptr) {
                    value = Value{EnumValue{static_cast<core::u16>(enumId), item->value}};
                }
            }
            else {
                value = readValue(property->type, json2, pending, id, atom, false);
            }

            if (!value.has_value()) {
                ++report.refusedProperties;
                continue;
            }
            if (world.setProperty(id, atom, *value) == World::SetResult::InvalidValue)
                ++report.refusedProperties;
            else
                ++report.properties;
        }
    }

    if (const JsonValue attributes = json["attributes"]; attributes.type() == core::JsonType::Object) {
        for (core::usize index = 0; index < attributes.size(); ++index) {
            const std::string_view name = attributes.keyAt(index);
            const JsonValue entry = attributes[name];
            // An attribute has no declared type, so its JSON shape is the only
            // thing that says what it is -- which is why the writer's encoding
            // has to stay unambiguous for the shapes an attribute can hold.
            std::optional<Value> value;
            switch (entry.type()) {
            case core::JsonType::Boolean:
                value = Value{entry.asBool()};
                break;
            case core::JsonType::Number:
                value = Value{entry.asNumber()};
                break;
            case core::JsonType::String:
                value = Value{std::string(entry.asString())};
                break;
            case core::JsonType::Array:
                if (entry.size() == 3)
                    value = Value{core::Vec3{static_cast<core::f32>(entry.at(0).asNumber()),
                                             static_cast<core::f32>(entry.at(1).asNumber()),
                                             static_cast<core::f32>(entry.at(2).asNumber())}};
                break;
            default:
                break;
            }
            if (value.has_value())
                (void)world.setAttribute(id, world.atoms().intern(name), *value);
            else
                ++report.refusedProperties;
        }
    }

    if (const JsonValue tags = json["tags"]; tags.type() == core::JsonType::Array) {
        for (core::usize index = 0; index < tags.size(); ++index)
            (void)world.addTag(id, world.atoms().intern(tags.at(index).asString()));
    }
}

core::InstanceId readInstance(World& world, core::InstanceId parent, const JsonValue& json,
                              std::vector<PendingReference>& pending, SceneIoReport& report, const StampSource* stamps,
                              int depth)
{
    // **A node that names a stamp is not built; it is STAMPED** (ADR 0049).
    // What the scene holds for it is a mark, a name and where it is, and
    // everything else comes from the stamp file -- which is the whole reason
    // the mark is worth having, and the reason changing a stamp changes every
    // unbroken instance of it.
    if (const std::string_view stampName = json["stamp"].asString(); !stampName.empty()) {
        const core::InstanceId placed = stamps != nullptr && *stamps && depth < kMaxStampDepth
                                            ? placeStamp(world, parent, stampName, report, stamps, depth)
                                            : core::InstanceId{};
        if (!placed.valid()) {
            // Counted rather than fatal, for the same reason an unknown class
            // is: a scene that names a stamp somebody deleted should still
            // open, minus what is gone.
            ++report.missingStamps;
            return {};
        }
        world.setName(placed, world.atoms().intern(json["name"].asString()));
        // The instance's own overrides, which the break rule limits to where it
        // is. `applyNode` rather than a hand-written `CFrame` case, so a rule
        // that ever widens widens in one place.
        applyNode(world, placed, json, pending, report);
        ++report.stamped;
        return placed;
    }

    const std::string_view className = json["class"].asString();
    const ClassId classId = world.classes().findId(world.atoms().intern(className));
    if (classId == InvalidClass) {
        // The whole subtree goes with it. A `Part` standing in for a class this
        // build does not have would be a lie shaped like a recovery, and the
        // children under it would be parented to something that is not what
        // they were authored against.
        ++report.unknownClasses;
        return {};
    }

    const core::InstanceId id = world.create(classId);
    world.setName(id, world.atoms().intern(json["name"].asString()));
    (void)world.setParent(id, parent);
    ++report.instances;

    applyNode(world, id, json, pending, report);

    if (const JsonValue children = json["children"]; children.type() == core::JsonType::Array) {
        for (core::usize index = 0; index < children.size(); ++index)
            readInstance(world, id, children.at(index), pending, report, stamps, depth);
    }
    return id;
}

[[nodiscard]] core::InstanceId resolvePath(const World& world, core::InstanceId root, std::string_view path)
{
    // The first segment names the root itself, which is how `collectPaths`
    // wrote it.
    core::usize cursor = path.find('.');
    core::InstanceId at = root;
    if (cursor == std::string_view::npos)
        return at;
    path.remove_prefix(cursor + 1);

    while (!path.empty()) {
        cursor = path.find('.');
        const std::string_view segment = cursor == std::string_view::npos ? path : path.substr(0, cursor);
        const core::NameAtom atom = world.atoms().lookup(segment);
        if (!atom.valid())
            return {};
        at = world.findFirstChild(at, atom);
        if (!at.valid())
            return {};
        if (cursor == std::string_view::npos)
            break;
        path.remove_prefix(cursor + 1);
    }
    return at;
}
// **A stamp's internal references resolve against the STAMP's own root**, not
// against the scene's. A path inside a stamp names something inside that stamp
// -- `Post.Lantern` is the lantern on this post -- and resolving it against the
// scene would find some other instance with that path, or nothing.
core::InstanceId placeStamp(World& world, core::InstanceId parent, std::string_view name, SceneIoReport& report,
                            const StampSource* stamps, int depth)
{
    const std::optional<std::string> text = (*stamps)(name);
    if (!text.has_value())
        return {};

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(*text); !parsed.ok)
        return {};

    const JsonValue root = document.root();
    if (root["format"].asString() != kFormat || root["version"].asInteger() != kVersion)
        return {};

    const JsonValue rootNode = root["root"];
    if (rootNode.type() != core::JsonType::Object)
        return {};

    std::vector<PendingReference> pending;
    const core::InstanceId placed = readInstance(world, parent, rootNode, pending, report, stamps, depth + 1);
    if (!placed.valid())
        return {};

    for (const PendingReference& reference : pending) {
        const core::InstanceId target = resolvePath(world, placed, reference.path);
        if (!target.valid()) {
            ++report.droppedReferences;
            continue;
        }
        if (reference.isAttribute)
            (void)world.setAttribute(reference.owner, reference.property, Value{target});
        else
            (void)world.setProperty(reference.owner, reference.property, Value{target});
    }

    world.setStamp(placed, world.atoms().intern(name));
    return placed;
}
} // namespace

void clearScene(World& world)
{
    const core::InstanceId workspace = workspaceOf(world);
    if (!workspace.valid())
        return;

    // Collected first. `destroy` unlinks as it goes, so walking and destroying
    // in one pass drops the rest of the list.
    std::vector<core::InstanceId> authored;
    for (core::InstanceId child = world.firstChild(workspace); child.valid(); child = world.nextSibling(child)) {
        // Not authored, so not a scene's to remove. A new scene is not a reason
        // to evict the ground a streaming system put there.
        if (world.generated(child))
            continue;
        authored.push_back(child);
    }
    for (const core::InstanceId child : authored)
        (void)world.destroy(child);
}

std::string writeScene(const World& world, SceneIoReport* report)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    JsonWriter writer;
    writer.beginObject();
    writer.field("format", kFormat);
    writer.field("version", kVersion);

    const core::InstanceId workspace = workspaceOf(world);
    if (workspace.valid()) {
        std::unordered_map<core::u32, std::string> paths;
        collectPaths(world, workspace, {}, paths);
        writer.key("root");
        writeInstance(writer, world, workspace, paths, out);
    }

    writer.endObject();
    return writer.text();
}

std::optional<core::EngineError> readScene(World& world, std::string_view json, SceneIoReport* report,
                                           const StampSource& stamps)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(json); !parsed.ok)
        return core::makeError(LUAUG_TR("scene.err.scene_parse"), {}, parsed.diagnostic);

    const JsonValue root = document.root();
    if (root["format"].asString() != kFormat)
        return core::makeError(LUAUG_TR("scene.err.scene_format"));
    if (root["version"].asInteger() != kVersion)
        return core::makeError(LUAUG_TR("scene.err.scene_version"));

    const core::InstanceId workspace = workspaceOf(world);
    if (!workspace.valid())
        return core::makeError(LUAUG_TR("scene.err.scene_no_workspace"));

    // Replacing, not merging: a scene IS the world's contents, and a load that
    // merged would double everything the second time it ran.
    clearScene(world);

    std::vector<PendingReference> pending;
    if (const JsonValue rootNode = root["root"]; rootNode.type() == core::JsonType::Object) {
        // The file's root IS the workspace, so its own properties apply to the
        // workspace and only its children are created.
        applyNode(world, workspace, rootNode, pending, out);
        if (const JsonValue children = rootNode["children"]; children.type() == core::JsonType::Array) {
            for (core::usize index = 0; index < children.size(); ++index)
                (void)readInstance(world, workspace, children.at(index), pending, out, &stamps, 0);
        }
    }

    for (const PendingReference& reference : pending) {
        const core::InstanceId target = resolvePath(world, workspace, reference.path);
        if (!target.valid()) {
            ++out.droppedReferences;
            continue;
        }
        if (reference.isAttribute)
            (void)world.setAttribute(reference.owner, reference.property, Value{target});
        else
            (void)world.setProperty(reference.owner, reference.property, Value{target});
    }

    return std::nullopt;
}

std::string writeStamp(const World& world, core::InstanceId root, SceneIoReport* report)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    JsonWriter writer;
    writer.beginObject();
    writer.field("format", kFormat);
    writer.field("version", kVersion);

    if (world.alive(root)) {
        std::unordered_map<core::u32, std::string> paths;
        collectPaths(world, root, {}, paths);
        writer.key("root");
        // `root`'s own mark is IGNORED, because this is the file that mark
        // points at: a stamp made from an instance of itself would otherwise
        // write a one-line file referring to the file being written.
        writeInstance(writer, world, root, paths, out, root);
    }

    writer.endObject();
    return writer.text();
}

core::InstanceId readStamp(World& world, std::string_view json, core::InstanceId parent, std::string_view stamp,
                           SceneIoReport* report)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    // The one-file case: the text is in hand, so the source it is read through
    // answers for this stamp and nothing else. A stamp naming another stamp is
    // refused at authoring time and would be a hand-edited file here.
    const StampSource source = [json, stamp](std::string_view wanted) -> std::optional<std::string> {
        return wanted == stamp ? std::optional<std::string>(std::string(json)) : std::nullopt;
    };
    return placeStamp(world, parent, stamp, out, &source, 0);
}

} // namespace luaug::scene
