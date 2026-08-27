#include <luaug/asset/terrain.h>
#include <luaug/asset/terrain_cell.h>
#include <luaug/core/base64.h>
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

// One stamp, built once and reused for every instance of it in a save. Its own
// world, so building it cannot touch the world being written.
struct StampLibrary::Entry
{
    std::unique_ptr<World> world;
    core::InstanceId root;
};

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

// --- Stamped instances, written as a mark plus what differs (ADR 0051) -------
//
// **The model the human asked for is INHERITANCE, not a copy.** An instance of
// a stamp follows its file: change the file and every instance changes with it.
// What an instance may have of its own is a set of property OVERRIDES -- "muda
// alguns parâmetros do novo sem influenciar o anterior" -- and those are the
// only thing about it this file records.
//
// So a stamped instance serialises as its mark, its name, its placement, and a
// map of overrides keyed by the path INSIDE the stamp. Nothing else, and none
// of its children: the children are the stamp's.
//
// **The diff is done against the stamp itself**, built into a scratch world
// once per stamp per save. There is no cheaper honest way: "what differs from
// the source" is a question about two trees, and comparing serialised text
// would compare formatting as well as values.

// The path of `id` inside the stamped subtree rooted at `stampRoot`: names
// joined by '.', and empty for the root itself.
//
// By NAME rather than by index, because a name is what a person reading the
// file can find, and the structural rule below means two children of one parent
// cannot both be overridden under one name without the link already being gone.
[[nodiscard]] std::string overridePath(const World& world, core::InstanceId stampRoot, core::InstanceId id)
{
    std::string path;
    for (core::InstanceId walk = id; walk.valid() && walk != stampRoot; walk = world.parentOf(walk)) {
        const std::string_view name = world.atoms().text(world.name(walk));
        path = path.empty() ? std::string(name) : std::string(name) + "." + path;
    }
    return path;
}

// Whether two subtrees have the same SHAPE: the same classes, in the same
// order, all the way down.
//
// **A structural change is not an override.** Adding a child to one lamp post,
// or deleting one, is not "a parameter of this instance" -- it is a different
// thing -- and a format that tried to record it would be inventing Unity's
// added-and-removed-component machinery in a corner nobody designed. When the
// shapes disagree the instance is written IN FULL and its mark dropped, which
// loses nothing and says what it did.
[[nodiscard]] bool sameShape(const World& live, core::InstanceId a, const World& reference, core::InstanceId b)
{
    const ClassDescriptor* liveClass = live.classes().find(live.classOf(a));
    const ClassDescriptor* referenceClass = reference.classes().find(reference.classOf(b));
    if (liveClass == nullptr || referenceClass == nullptr)
        return false;
    if (live.atoms().text(liveClass->name) != reference.atoms().text(referenceClass->name))
        return false;
    if (live.childCount(a) != reference.childCount(b))
        return false;

    core::InstanceId liveChild = live.firstChild(a);
    core::InstanceId referenceChild = reference.firstChild(b);
    while (liveChild.valid() && referenceChild.valid()) {
        if (!sameShape(live, liveChild, reference, referenceChild))
            return false;
        liveChild = live.nextSibling(liveChild);
        referenceChild = reference.nextSibling(referenceChild);
    }
    return !liveChild.valid() && !referenceChild.valid();
}

// The path of `id` under `root`, or nothing at all when `id` is not under it.
// The root itself is the EMPTY path, which is why this answers with an optional
// rather than a string a caller has to test for emptiness -- "not in this
// subtree" and "is this subtree" are different answers and one of them is a
// reference the file has to record.
[[nodiscard]] std::optional<std::string> pathUnder(const World& world, core::InstanceId root, core::InstanceId id)
{
    if (!id.valid())
        return std::nullopt;
    std::string path;
    for (core::InstanceId walk = id; walk.valid(); walk = world.parentOf(walk)) {
        if (walk == root)
            return path;
        const std::string_view name = world.atoms().text(world.name(walk));
        path = path.empty() ? std::string(name) : std::string(name) + "." + path;
    }
    return std::nullopt;
}

// Emits the properties of `liveId` that differ from `refId`, under
// `overridePath`. Recurses into children by position.
//
// **An instance-valued property is compared by PATH, not by id** (D142). The
// live value names an instance in the live world and the reference names one in
// the stamp's own, so the two ids are not comparable -- which is true, and was
// for a long time the reason this skipped every `ValueType::Instance` property
// outright. That skipped two things it should not have: a reference RE-POINTED
// at a different member of the stamp, and a reference to anything BESIDE the
// stamp rather than under it. Both were dropped silently. The second is how a
// `Material` assigned to a part inside a placed stamp disappeared on save --
// the placed-stamp twin of D133, and it did not even reach the writer that
// counts a dropped reference.
//
// Ids are not comparable; paths under each subtree's own root are. Equal paths
// mean the stamp's own reference, untouched, and that is not an override --
// which is the case the blanket skip was protecting, because recording one per
// placed instance would turn a mark and a placement into a claim about an edit
// nobody made. Anything else is written as a full scene path, which the loader
// has resolved through its deferred pass since the format existed.
// Whether one property of one instance differs from the same property of the
// instance it was stamped from.
//
// **One definition, asked by two callers** (S5.6). The save writes what differs;
// the Properties panel MARKS what differs, and offers to revert or apply it. If
// those two answered the question separately they would disagree the first time
// either was touched, and the panel would offer to revert something the save had
// already decided was not an override -- which reads as the editor lying about
// what it is holding.
[[nodiscard]] bool differsFromReference(const World& live, const PropertyDesc& property, std::string_view name,
                                        const World& reference, core::InstanceId refId, core::InstanceId stampRoot,
                                        core::InstanceId referenceRoot, const Value& mine)
{
    // The same property on the reference, found by NAME because the two worlds
    // share their registries but not their ids.
    const core::NameAtom referenceAtom = reference.atoms().lookup(name);
    const PropertyDesc* referenceProperty =
        referenceAtom.valid() ? reference.classes().findProperty(reference.classOf(refId), referenceAtom) : nullptr;
    std::optional<Value> theirs;
    if (referenceProperty != nullptr && referenceProperty->get != nullptr)
        theirs = referenceProperty->get(reference, refId);

    // **`get_if`, and the descriptor's declared type is not enough to justify
    // `get`.** A getter answers `Value{}` -- Nil -- for an instance whose
    // component is not there, which every accessor in the tree does and which is
    // not an error; `std::get` on that throws `bad_variant_access`, and an
    // exception raised here is not caught anywhere between this and `main`. It
    // terminated the editor on save, in a project a person was in the middle of
    // building (D140). Ask what the value HOLDS, never what the schema says it
    // should.
    if (property.type == ValueType::Instance) {
        const core::InstanceId* liveHeld = std::get_if<core::InstanceId>(&mine);
        const core::InstanceId* refHeld = theirs.has_value() ? std::get_if<core::InstanceId>(&*theirs) : nullptr;
        const core::InstanceId liveTarget = liveHeld != nullptr ? *liveHeld : core::InstanceId{};
        const core::InstanceId refTarget = refHeld != nullptr ? *refHeld : core::InstanceId{};
        if (!liveTarget.valid() && !refTarget.valid())
            return false;
        // Ids are not comparable; paths under each subtree's own root are. Equal
        // paths mean the stamp's own reference untouched, which is not an
        // override -- recording one per placed instance would turn a mark and a
        // placement into a claim about an edit nobody made (D142).
        const std::optional<std::string> livePath = pathUnder(live, stampRoot, liveTarget);
        const std::optional<std::string> refPath = pathUnder(reference, referenceRoot, refTarget);
        if (livePath.has_value() && refPath.has_value() && *livePath == *refPath)
            return false;
        return true;
    }

    return !theirs.has_value() || !(*theirs == mine);
}

void collectOverrides(JsonWriter& out, bool& anyOverride, const World& live, core::InstanceId liveId,
                      const World& reference, core::InstanceId refId, core::InstanceId stampRoot,
                      core::InstanceId referenceRoot, const std::unordered_map<core::u32, std::string>& paths,
                      SceneIoReport& report)
{
    const ClassDescriptor* descriptor = live.classes().find(live.classOf(liveId));
    bool anyHere = false;

    for (const ClassDescriptor* current = descriptor; current != nullptr;
         current = live.classes().find(current->super)) {
        for (const PropertyDesc& property : current->properties) {
            if (property.get == nullptr || property.set == nullptr || property.readOnly)
                continue;
            const std::string_view name = live.atoms().text(property.name);
            if (isStructuralProperty(name))
                continue;

            const std::optional<Value> mine = property.get(live, liveId);
            if (!mine.has_value())
                continue;

            if (!differsFromReference(live, property, name, reference, refId, stampRoot, referenceRoot, *mine))
                continue;
            if (!anyHere) {
                if (!anyOverride) {
                    out.key("overrides");
                    out.beginObject();
                    anyOverride = true;
                }
                out.key(overridePath(live, stampRoot, liveId));
                out.beginObject();
                anyHere = true;
            }
            out.key(name);
            writeValue(out, live, *mine, paths, report);
            ++report.overrides;
        }
    }
    if (anyHere)
        out.endObject();

    core::InstanceId liveChild = live.firstChild(liveId);
    core::InstanceId referenceChild = reference.firstChild(refId);
    while (liveChild.valid() && referenceChild.valid()) {
        collectOverrides(out, anyOverride, live, liveChild, reference, referenceChild, stampRoot, referenceRoot, paths,
                         report);
        liveChild = live.nextSibling(liveChild);
        referenceChild = reference.nextSibling(referenceChild);
    }
}

// `expandStamped` is the instance whose own stamp mark is IGNORED, and there is
// exactly one situation with one: writing the stamp FILE, whose root is an
// instance of the stamp it is being written from. Every other stamped instance
// collapses to its mark.
void writeInstance(JsonWriter& out, const World& world, core::InstanceId id,
                   const std::unordered_map<core::u32, std::string>& paths, SceneIoReport& report,
                   core::InstanceId expandStamped = core::InstanceId{}, StampLibrary* stamps = nullptr)
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
        const std::string stampName(world.atoms().text(stamp));
        const StampLibrary::Entry* reference = stamps != nullptr ? stamps->reference(stampName) : nullptr;

        // **The shape has to match, or this is not an instance of that stamp
        // any more.** Somebody added a child, or deleted one, or the file moved
        // on structurally -- and a format that tried to record THAT would be
        // inventing an added-and-removed-object machinery nobody designed. The
        // instance is written in full instead, which loses nothing, and the
        // count says it happened.
        if (reference != nullptr && sameShape(world, id, *reference->world, reference->root)) {
            out.field("stamp", stampName);
            bool anyOverride = false;
            collectOverrides(out, anyOverride, world, id, *reference->world, reference->root, id, reference->root,
                             paths, report);
            if (anyOverride)
                out.endObject();
            out.endObject();
            ++report.stamped;
            return;
        }
        ++report.unlinkedStamps;
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

    // **The ground, because a sculpted world is somebody's afternoon.**
    //
    // Terrain is the one piece of world state that is not a property: a field is
    // tiles and bricks rather than a number, so nothing in the property loop
    // above can reach it, and for one commit a save wrote every instance in the
    // scene and none of the ground.
    //
    // Its own key beside `attributes` and `tags` rather than a side-car file,
    // and the reason is the contract: `writeScene` returns a string and a scene
    // is one text file, which the editor, the packager, `readScene` and every
    // round-trip test all depend on. Base64 keeps that true at a size the cell
    // format's run coder makes reasonable.
    //
    // **`.lterrain`, the same format a streamed cell uses, and not a second
    // one.** Two encoders for one thing is two behaviours for one thing, which
    // is the argument this repository already made about importers. An authored
    // world is written as the cell at the origin -- the whole field, unsplit --
    // and when streaming arrives that field is what gets divided into real
    // cells. A field with nothing in it writes nothing, which is what keeps
    // every existing scene byte-identical.
    if (const TerrainComponent* terrain = world.terrains().find(id); terrain != nullptr) {
        if (terrain->field.tileCount() > 0 || terrain->field.brickCount() > 0) {
            asset::TerrainCell cell;
            cell.settings = terrain->field.settings();
            cell.field = terrain->field;
            const std::vector<std::byte> encoded = asset::encodeTerrainCell(cell);
            out.field("terrain", core::base64Encode(std::span<const core::u8>{
                                     reinterpret_cast<const core::u8*>(encoded.data()), encoded.size()}));
            ++report.properties;
        }
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
            writeInstance(out, world, child, paths, report, expandStamped, stamps);
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

// The seed a reference world is built with. A constant: nothing in one is
// simulated and nothing reads the generator, and a seed from anywhere else
// would make a save's bytes depend on when it ran.
constexpr core::u64 kReferenceSeed = 0x5245'4645u;

// Reads the stamp `name` through the caller's source and builds it under
// `parent`, marked. An invalid id means the source had nothing, the text was
// not a stamp, or the class it names is one this build does not have.
[[nodiscard]] core::InstanceId placeStamp(World& world, core::InstanceId parent, std::string_view name,
                                          SceneIoReport& report, const StampSource* stamps, int depth);

// A dotted path BELOW `root`. Defined beside `resolvePath`, declared here
// because a stamped node applies its overrides through it.
[[nodiscard]] core::InstanceId resolveInside(const World& world, core::InstanceId root, std::string_view path);

// The properties in one JSON object, applied to one instance.
//
// Split out of `applyNode` because a stamped instance's OVERRIDES are exactly
// this shape at a path inside it (ADR 0051) -- and two copies of "how a
// property is read back" would disagree the first time either moved.
void applyProperties(World& world, core::InstanceId id, const JsonValue& properties,
                     std::vector<PendingReference>& pending, SceneIoReport& report)
{
    const ClassId classId = world.classOf(id);
    if (properties.type() == core::JsonType::Object) {
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
}

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
    applyProperties(world, id, json["properties"], pending, report);

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

    // The ground. Counted as one property either way, so a load that could not
    // read it says so in the same report a dropped reference would.
    if (const JsonValue terrain = json["terrain"]; terrain.type() == core::JsonType::String) {
        TerrainComponent* component = world.terrains().find(id);
        if (component != nullptr) {
            const std::optional<std::vector<core::u8>> bytes = core::base64Decode(terrain.asString());
            asset::TerrainCell cell;
            const bool decoded =
                bytes.has_value() &&
                !asset::decodeTerrainCell(
                     std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes->data()), bytes->size()}, cell)
                     .has_value();
            if (decoded) {
                component->field = std::move(cell.field);
                // **The settings ride with the field**, so `MinHeight` and
                // `MaxHeight` are whatever the ground was actually sculpted
                // under rather than whatever the properties happened to say.
                // They cannot be widened after a collider is built (ADR 0066),
                // and a reserved range that disagreed with the ground in it
                // would clamp every later edit to the wrong band.
                component->minHeight = component->field.settings().minHeight;
                component->maxHeight = component->field.settings().maxHeight;
                component->fieldRevision += 1;
                ++report.properties;
            }
            else {
                ++report.droppedReferences;
            }
        }
    }
}

// **What an instance has of its own** (ADR 0051), keyed by the path inside the
// stamp: `""` is the instance itself and `Lantern.Bulb` is something under it.
//
// Applied AFTER the stamp has been built, which is what makes an override an
// override -- the stamp says what a thing is and these say what this one of them
// is like. One function rather than two, because a load and a live refresh
// (`restamp`) put the same overrides back on top of the same file and two
// spellings of that would disagree the first time either moved.
void applyOverrides(World& world, core::InstanceId placed, const JsonValue& overrides,
                    std::vector<PendingReference>& pending, SceneIoReport& report)
{
    if (overrides.type() != core::JsonType::Object)
        return;

    for (core::usize index = 0; index < overrides.size(); ++index) {
        const std::string_view path = overrides.keyAt(index);
        const core::InstanceId target = path.empty() ? placed : resolveInside(world, placed, path);
        if (!target.valid()) {
            // The stamp moved on and no longer has what this override names.
            // Counted rather than fatal, exactly as an unknown class is: a scene
            // should still open, minus what is gone.
            ++report.refusedProperties;
            continue;
        }
        applyProperties(world, target, overrides[path], pending, report);
        ++report.overrides;
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

        applyOverrides(world, placed, json["overrides"], pending, report);
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

// A dotted path BELOW `root`, where `resolvePath` takes one whose first segment
// names the root itself. Two functions rather than a flag, because the two
// shapes come from two formats and conflating them is how a path resolves to
// the wrong instance in exactly one of them.
[[nodiscard]] core::InstanceId resolveInside(const World& world, core::InstanceId root, std::string_view path)
{
    core::InstanceId at = root;
    while (!path.empty() && at.valid()) {
        const core::usize cursor = path.find('.');
        const std::string_view segment = cursor == std::string_view::npos ? path : path.substr(0, cursor);
        const core::NameAtom atom = world.atoms().lookup(segment);
        if (!atom.valid())
            return {};
        at = world.findFirstChild(at, atom);
        if (cursor == std::string_view::npos)
            break;
        path.remove_prefix(cursor + 1);
    }
    return at;
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

StampLibrary::StampLibrary(World& registriesFrom, StampSource source)
    : m_registries(registriesFrom), m_source(std::move(source))
{}

StampLibrary::~StampLibrary() = default;

const StampLibrary::Entry* StampLibrary::reference(const std::string& stamp)
{
    if (const auto found = m_built.find(stamp); found != m_built.end())
        return found->second == nullptr ? nullptr : found->second.get();

    // **A stamp that cannot be read is remembered as unreadable**, so a world
    // with forty instances of a deleted stamp asks the filesystem once.
    if (!m_source) {
        m_built.emplace(stamp, nullptr);
        return nullptr;
    }
    const std::optional<std::string> text = m_source(stamp);
    if (!text.has_value()) {
        m_built.emplace(stamp, nullptr);
        return nullptr;
    }

    auto entry = std::make_unique<Entry>();
    entry->world =
        std::make_unique<World>(m_registries.classes(), m_registries.enums(), m_registries.atoms(), kReferenceSeed);
    SceneIoReport ignored;
    // Unparented, because a reference tree is never looked at through a
    // hierarchy -- only walked from its root.
    entry->root = readStamp(*entry->world, *text, core::InstanceId{}, stamp, &ignored);
    if (!entry->root.valid()) {
        m_built.emplace(stamp, nullptr);
        return nullptr;
    }

    const Entry* raw = entry.get();
    m_built.emplace(stamp, std::move(entry));
    return raw;
}

std::string writeScene(const World& world, SceneIoReport* report, StampLibrary* stamps)
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
        writeInstance(writer, world, workspace, paths, out, core::InstanceId{}, stamps);
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

core::InstanceId readSceneNode(World& world, std::string_view nodeJson, core::InstanceId parent, SceneIoReport* report,
                               const StampSource& stamps)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(nodeJson); !parsed.ok)
        return {};

    const JsonValue node = document.root();
    if (node.type() != core::JsonType::Object)
        return {};

    std::vector<PendingReference> pending;
    const core::InstanceId built = readInstance(world, parent, node, pending, out, &stamps, 0);
    if (!built.valid())
        return {};

    // Resolved against the NODE rather than against a scene root, because that
    // is what this subtree has: a path leaving it names something the caller's
    // document holds and this world does not, and it is dropped with a count
    // exactly as a scene drops one leaving the scene.
    for (const PendingReference& reference : pending) {
        const core::InstanceId target = resolvePath(world, built, reference.path);
        if (!target.valid()) {
            ++out.droppedReferences;
            continue;
        }
        if (reference.isAttribute)
            (void)world.setAttribute(reference.owner, reference.property, Value{target});
        else
            (void)world.setProperty(reference.owner, reference.property, Value{target});
    }
    return built;
}

std::string writeStamp(const World& world, core::InstanceId root, SceneIoReport* report, StampLibrary* stamps)
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
        writeInstance(writer, world, root, paths, out, root, stamps);
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

core::u32 restamp(World& world, core::InstanceId root, std::string_view stamp, std::string_view before,
                  std::string_view after, SceneIoReport* report)
{
    SceneIoReport local;
    SceneIoReport& out = report != nullptr ? *report : local;

    const core::NameAtom mark = world.atoms().lookup(stamp);
    if (!mark.valid() || !world.alive(root))
        return 0;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(after); !parsed.ok)
        return 0;
    const JsonValue file = document.root();
    const JsonValue rootNode = file["root"];
    if (file["format"].asString() != kFormat || file["version"].asInteger() != kVersion ||
        rootNode.type() != core::JsonType::Object)
        return 0;

    // **The file as the live instances were built from it**, in a world of its
    // own. "What has this one got of its own" is a question about two trees and
    // there is no cheaper honest way to ask it -- the same argument the writer
    // makes, one save earlier.
    World reference(world.classes(), world.enums(), world.atoms(), kReferenceSeed);
    SceneIoReport ignored;
    const core::InstanceId referenceRoot = readStamp(reference, before, {}, stamp, &ignored);
    if (!referenceRoot.valid())
        return 0;

    const ClassId rootClass = world.classes().findId(world.atoms().intern(rootNode["class"].asString()));
    if (rootClass == InvalidClass)
        return 0;

    // Collected before anything is touched: the walk and the rebuild cannot be
    // one pass, because the rebuild replaces the children the walk is standing
    // in.
    std::vector<core::InstanceId> subtree;
    subtree.push_back(root);
    world.collectDescendants(root, subtree);

    const StampSource source = [after, stamp](std::string_view wanted) -> std::optional<std::string> {
        return wanted == stamp ? std::optional<std::string>(std::string(after)) : std::nullopt;
    };

    core::u32 refreshed = 0;
    for (const core::InstanceId target : subtree) {
        if (!world.alive(target) || world.stampOf(target) != mark)
            continue;

        // **Not an instance of that stamp any more**, so it is left exactly as
        // it is and counted. Somebody added a child to this one, or deleted
        // one, and rebuilding it from the file would throw that away -- which is
        // the same rule the writer applies for the same reason.
        if (world.classOf(target) != rootClass || !sameShape(world, target, reference, referenceRoot)) {
            ++out.unlinkedStamps;
            continue;
        }

        // What it has of its own, measured against the file it came from and
        // kept as text: the tree it was measured on is about to stop existing.
        std::unordered_map<core::u32, std::string> paths;
        JsonWriter kept;
        kept.beginObject();
        bool anyOverride = false;
        SceneIoReport measured;
        collectOverrides(kept, anyOverride, world, target, reference, referenceRoot, target, referenceRoot, paths,
                         measured);
        if (anyOverride)
            kept.endObject();
        kept.endObject();

        core::JsonDocument overrides;
        const bool hasOverrides = anyOverride && overrides.parse(kept.text()).ok;

        // **The instance itself survives**: its id, its parent, its place among
        // its siblings and every reference anything else holds to it. Destroying
        // and rebuilding it would move it to the end of its parent, and an
        // Explorer whose rows jump every time somebody saves is one nobody
        // trusts.
        std::vector<core::InstanceId> children;
        world.collectChildren(target, children);
        for (const core::InstanceId child : children)
            (void)world.destroy(child);

        std::vector<PendingReference> pending;
        applyNode(world, target, rootNode, pending, out);
        if (const JsonValue nodes = rootNode["children"]; nodes.type() == core::JsonType::Array) {
            for (core::usize index = 0; index < nodes.size(); ++index)
                (void)readInstance(world, target, nodes.at(index), pending, out, &source, 1);
        }
        if (hasOverrides)
            applyOverrides(world, target, overrides.root()["overrides"], pending, out);

        // Inside the stamp, against the stamp's own root -- the rule
        // `placeStamp` states and for the same reason: a path inside a stamp
        // names something inside that stamp.
        for (const PendingReference& entry : pending) {
            const core::InstanceId found = resolvePath(world, target, entry.path);
            if (!found.valid()) {
                ++out.droppedReferences;
                continue;
            }
            if (entry.isAttribute)
                (void)world.setAttribute(entry.owner, entry.property, Value{found});
            else
                (void)world.setProperty(entry.owner, entry.property, Value{found});
        }

        ++out.stamped;
        ++refreshed;
    }

    return refreshed;
}

namespace {

// Where an instance sits inside the stamp it came from: the stamp's root in the
// LIVE world, and the instance that corresponds to it in the stamp's own tree.
//
// Shared by the two questions a panel asks -- which properties are overridden,
// and what the stamp says one of them should be -- because a second copy of this
// walk is a second chance to pair the wrong instances.
struct ReferenceSite
{
    const World* world = nullptr;
    core::InstanceId id;
    core::InstanceId referenceRoot;
    core::InstanceId stampRoot;

    [[nodiscard]] bool found() const noexcept { return world != nullptr && id.valid(); }
};

[[nodiscard]] ReferenceSite locateInStamp(const World& world, core::InstanceId id, StampLibrary& stamps)
{
    if (!world.alive(id))
        return {};

    // **Up to the nearest stamped ancestor, including `id` itself.** A person
    // selects the part inside the lamp post as readily as the lamp post, and
    // both questions are the same one measured from the same root.
    core::InstanceId stampRoot = id;
    core::NameAtom mark{};
    while (stampRoot.valid()) {
        mark = world.stampOf(stampRoot);
        if (mark.valid())
            break;
        stampRoot = world.parentOf(stampRoot);
    }
    if (!stampRoot.valid() || !mark.valid())
        return {};

    const StampLibrary::Entry* entry = stamps.reference(std::string(world.atoms().text(mark)));
    if (entry == nullptr || entry->world == nullptr || !entry->root.valid())
        return {};

    // The child indices from the stamp root down to `id`, then the same walk
    // from the reference's root. Indices rather than names, because two siblings
    // may share a name and the save pairs them positionally too.
    std::vector<core::u32> descent;
    for (core::InstanceId step = id; step != stampRoot; step = world.parentOf(step)) {
        const core::InstanceId parent = world.parentOf(step);
        if (!parent.valid())
            return {};
        core::u32 index = 0;
        core::InstanceId child = world.firstChild(parent);
        while (child.valid() && child != step) {
            child = world.nextSibling(child);
            ++index;
        }
        if (!child.valid())
            return {};
        descent.push_back(index);
    }

    const World& reference = *entry->world;
    core::InstanceId refId = entry->root;
    for (auto step = descent.rbegin(); step != descent.rend(); ++step) {
        core::InstanceId child = reference.firstChild(refId);
        for (core::u32 skipped = 0; skipped < *step && child.valid(); ++skipped)
            child = reference.nextSibling(child);
        if (!child.valid())
            return {};
        refId = child;
    }

    // A different class is a different instance, not an instance with every
    // property overridden.
    if (world.classOf(id) != reference.classOf(refId))
        return {};

    return ReferenceSite{&reference, refId, entry->root, stampRoot};
}

} // namespace

std::optional<Value> stampReferenceValue(const World& world, core::InstanceId id, core::NameAtom property,
                                         StampLibrary& stamps)
{
    const ReferenceSite site = locateInStamp(world, id, stamps);
    if (!site.found())
        return std::nullopt;

    const std::string_view name = world.atoms().text(property);
    const core::NameAtom referenceAtom = site.world->atoms().lookup(name);
    if (!referenceAtom.valid())
        return std::nullopt;
    const PropertyDesc* referenceProperty =
        site.world->classes().findProperty(site.world->classOf(site.id), referenceAtom);
    if (referenceProperty == nullptr || referenceProperty->get == nullptr)
        return std::nullopt;
    return referenceProperty->get(*site.world, site.id);
}

std::vector<core::NameAtom> stampOverrides(const World& world, core::InstanceId id, StampLibrary& stamps)
{
    if (!world.alive(id))
        return {};

    const ReferenceSite site = locateInStamp(world, id, stamps);
    if (!site.found())
        return {};

    const World& reference = *site.world;
    const core::InstanceId refId = site.id;
    const core::InstanceId stampRoot = site.stampRoot;

    std::vector<core::NameAtom> overridden;
    const ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    for (const ClassDescriptor* current = descriptor; current != nullptr;
         current = world.classes().find(current->super)) {
        for (const PropertyDesc& property : current->properties) {
            if (property.get == nullptr || property.set == nullptr || property.readOnly)
                continue;
            const std::string_view name = world.atoms().text(property.name);
            if (isStructuralProperty(name))
                continue;
            const std::optional<Value> mine = property.get(world, id);
            if (!mine.has_value())
                continue;
            if (differsFromReference(world, property, name, reference, refId, stampRoot, site.referenceRoot, *mine))
                overridden.push_back(property.name);
        }
    }
    return overridden;
}

} // namespace luaug::scene
