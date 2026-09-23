#include "luaug/app/scene_definitions.h"

#include "luaug/platform/file.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <vector>

namespace luaug::app {
namespace {

// Deep enough for any tree a person builds by hand, and a stop for one that is
// generated past sense.
constexpr int MaxDepth = 32;

constexpr std::array<std::string_view, 22> Keywords{
    "and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "if",    "in",
    "local", "nil",   "not", "or",   "repeat", "return", "then",  "true", "until",    "while", "continue"};

// A table-type key: bare when it can be, quoted when it cannot -- `["Lantern
// Post"]` reads exactly as `workspace["Lantern Post"]` does.
[[nodiscard]] std::string keyOf(std::string_view name)
{
    const bool identifier =
        !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) != 0 || name[0] == '_') &&
        std::all_of(name.begin(), name.end(),
                    [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }) &&
        std::find(Keywords.begin(), Keywords.end(), name) == Keywords.end();
    if (identifier)
        return std::string(name);
    std::string quoted = "[\"";
    for (const char c : name) {
        if (c == '"' || c == '\\')
            quoted += '\\';
        if (c == '\n') {
            quoted += "\\n";
            continue;
        }
        quoted += c;
    }
    quoted += "\"]";
    return quoted;
}

[[nodiscard]] std::string_view classNameOf(const scene::World& world, core::InstanceId id)
{
    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    return descriptor != nullptr ? world.atoms().text(descriptor->name) : std::string_view{"Instance"};
}

// `Class` for a leaf, `Class & { ... }` for an instance with declared children.
void emitType(const scene::World& world, core::InstanceId id, int depth, std::string& out);

void emitChildren(const scene::World& world, core::InstanceId parent, int depth, std::string& out)
{
    const scene::ClassId parentClass = world.classOf(parent);
    std::vector<core::NameAtom> seen;
    std::string body;
    for (core::InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
        if (world.generated(child))
            continue;
        const core::NameAtom name = world.name(child);
        if (!name.valid() || std::find(seen.begin(), seen.end(), name) != seen.end())
            continue;
        seen.push_back(name);
        // A member of the parent wins over a child of its name at run time, so
        // declaring the child would be declaring something a dot never reaches.
        if (world.classes().findProperty(parentClass, name) != nullptr ||
            world.classes().findMethod(parentClass, name) != nullptr ||
            world.classes().findEvent(parentClass, name) != nullptr)
            continue;
        body.append(static_cast<std::size_t>(4 * (depth + 1)), ' ');
        body += keyOf(world.atoms().text(name));
        body += ": ";
        emitType(world, child, depth + 1, body);
        body += ",\n";
    }
    out += body;
}

void emitType(const scene::World& world, core::InstanceId id, int depth, std::string& out)
{
    out += classNameOf(world, id);
    if (depth >= MaxDepth || !world.firstChild(id).valid())
        return;
    std::string children;
    emitChildren(world, id, depth, children);
    if (children.empty())
        return;
    out += " & {\n";
    out += children;
    out.append(static_cast<std::size_t>(4 * depth), ' ');
    out += '}';
}

} // namespace

std::string sceneDefinitions(const scene::World& world)
{
    core::InstanceId workspace;
    world.workspaces().forEach([&](core::InstanceId id, const auto&) {
        if (!workspace.valid() && world.alive(id))
            workspace = id;
    });

    std::string out;
    out += "-- What `workspace` holds in this project's scene, as types: written by the engine\n";
    out += "-- from the scene itself, on every save in the editor and by `luaug setup`. Do not\n";
    out += "-- edit it: the next save replaces it. It is loaded after engine.d.luau, and this\n";
    out += "-- declaration replaces the plain `declare workspace: Workspace` there.\n\n";
    out += "declare workspace: ";
    if (workspace.valid())
        emitType(world, workspace, 0, out);
    else
        out += "Workspace";
    out += '\n';
    return out;
}

bool writeSceneDefinitions(const scene::World& world, const std::filesystem::path& project)
{
    const std::filesystem::path types = project / ".luaug" / "types";
    return platform::createDirectories(types) &&
           platform::writeTextFile(types / "scene.d.luau", sceneDefinitions(world));
}

} // namespace luaug::app
