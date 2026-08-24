#include "luaug/app/launcher.h"

#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/text_key.h"
#include "luaug/core/toml.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <system_error>

namespace luaug::app {
namespace {

constexpr std::string_view kFormat = "luaug-projects";

// How many rows the file keeps. A recent list is a shortcut, not an archive, and
// an unbounded one is a file that grows for as long as somebody uses the engine.
constexpr core::usize kMaxEntries = 32;

// The one placeholder the template carries, and the same one `luaug new`
// substitutes. Named here so the two implementations disagree at compile time
// rather than in somebody's scaffolded project.
constexpr std::string_view kNamePlaceholder = "{{name}}";

// A path in the one spelling the list compares by. `weakly_canonical` rather
// than `canonical` because a remembered project may no longer exist, and asking
// about a missing directory must answer rather than throw.
[[nodiscard]] std::filesystem::path normalised(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec)
        return path.lexically_normal();
    return resolved;
}

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    if (from.empty())
        return;
    for (std::string::size_type at = text.find(from, 0); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

// Copies one template tree, substituting as it goes.
//
// Text, deliberately: every file in the template is text (`templates/README.md`
// says the tree is analysed by CI exactly as written), and a byte copy would
// mean a second pass to find the ones needing substitution. A binary file
// appearing here would be a change to the template, and this is where it would
// have to be noticed.
[[nodiscard]] bool copyTemplate(const std::filesystem::path& from, const std::filesystem::path& to,
                                std::string_view name)
{
    if (!platform::createDirectories(to))
        return false;

    std::error_code ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(from, ec)) {
        if (ec)
            return false;
        const std::filesystem::path target = to / entry.path().filename();
        if (entry.is_directory(ec)) {
            if (!copyTemplate(entry.path(), target, name))
                return false;
            continue;
        }
        if (!entry.is_regular_file(ec))
            continue;

        std::string text;
        if (!platform::readTextFile(entry.path(), text))
            return false;
        replaceAll(text, kNamePlaceholder, name);
        if (!platform::writeTextFile(target, text))
            return false;
    }
    return !ec;
}

} // namespace

bool isProjectDirectory(const std::filesystem::path& directory)
{
    std::error_code ec;
    if (directory.empty() || !std::filesystem::is_directory(directory, ec))
        return false;
    return std::filesystem::exists(directory / "luaug.toml", ec) ||
           std::filesystem::is_directory(directory / "src" / "scripts", ec);
}

std::string projectNameOf(const std::filesystem::path& directory)
{
    std::string text;
    if (platform::readTextFile(directory / "luaug.toml", text)) {
        core::TomlDocument document;
        if (document.parse(text, "luaug.toml")) {
            if (const std::optional<std::string_view> named = document.string("project.name");
                named.has_value() && !named->empty()) {
                return std::string(*named);
            }
        }
    }

    // The directory's own name, which is what an unnamed project is called
    // everywhere else -- `luaug build` picks the same fallback.
    const std::filesystem::path leaf = directory.filename();
    return leaf.empty() ? directory.string() : leaf.string();
}

void ProjectList::load(const std::filesystem::path& file)
{
    file_ = file;
    entries_.clear();

    std::string text;
    if (file.empty() || !platform::readTextFile(file, text))
        return;

    core::JsonDocument document;
    if (!document.parse(text, file.string()))
        return;

    const core::JsonValue root = document.root();
    if (root["format"].asString() != kFormat)
        return;

    const core::JsonValue projects = root["projects"];
    for (core::usize i = 0; i < projects.size() && entries_.size() < kMaxEntries; ++i) {
        const std::string_view stored = projects.at(i)["path"].asString();
        if (stored.empty())
            continue;
        RecentProject entry;
        entry.path = normalised(std::filesystem::path(stored));
        // Deduplicated on the way in as well as on the way out: a file somebody
        // edited by hand is still a file this has to answer for.
        const auto same = [&](const RecentProject& other) { return other.path == entry.path; };
        if (std::find_if(entries_.begin(), entries_.end(), same) != entries_.end())
            continue;
        entries_.push_back(std::move(entry));
    }

    refresh();
}

bool ProjectList::save() const
{
    if (file_.empty())
        return false;

    core::JsonWriter json;
    json.beginObject();
    json.field("format", kFormat);
    json.key("projects");
    json.beginArray();
    for (const RecentProject& entry : entries_) {
        json.beginObject();
        // The path and nothing else. A name is read from the project every time
        // this is loaded, so storing one would be a copy that goes stale the
        // first time somebody renames their game.
        json.field("path", entry.path.generic_string());
        json.endObject();
    }
    json.endArray();
    json.endObject();

    return platform::createDirectories(file_.parent_path()) && platform::writeTextFile(file_, json.text());
}

void ProjectList::remember(const std::filesystem::path& path)
{
    const std::filesystem::path key = normalised(path);
    if (key.empty())
        return;

    std::erase_if(entries_, [&](const RecentProject& entry) { return entry.path == key; });

    RecentProject entry;
    entry.path = key;
    entry.name = projectNameOf(key);
    entry.missing = !isProjectDirectory(key);
    entries_.insert(entries_.begin(), std::move(entry));

    if (entries_.size() > kMaxEntries)
        entries_.resize(kMaxEntries);
}

void ProjectList::forget(const std::filesystem::path& path)
{
    const std::filesystem::path key = normalised(path);
    std::erase_if(entries_, [&](const RecentProject& entry) { return entry.path == key; });
}

void ProjectList::refresh()
{
    for (RecentProject& entry : entries_) {
        entry.missing = !isProjectDirectory(entry.path);
        // A missing project keeps the last name it had, and gets its directory's
        // when it never had one: a row reading as an empty string is a row
        // nobody can identify well enough to remove on purpose.
        if (!entry.missing || entry.name.empty())
            entry.name = projectNameOf(entry.path);
    }
}

std::vector<std::string> availableTemplates(const std::filesystem::path& templatesDir)
{
    std::vector<std::string> names;
    std::error_code ec;
    if (templatesDir.empty() || !std::filesystem::is_directory(templatesDir, ec))
        return names;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(templatesDir, ec)) {
        if (ec)
            break;
        if (entry.is_directory(ec))
            names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool validProjectName(std::string_view name)
{
    if (name.empty())
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

NewProjectResult createProject(const std::filesystem::path& templatesDir, const std::filesystem::path& definitions,
                               const NewProjectRequest& request)
{
    NewProjectResult result;

    if (!validProjectName(request.name)) {
        result.error = core::makeError(LUAUG_TR("app.err.launcher_bad_name"), {}, request.name);
        return result;
    }

    const std::filesystem::path source = templatesDir / request.templateName;
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec)) {
        result.error = core::makeError(LUAUG_TR("app.err.launcher_no_template"), {}, source.string());
        return result;
    }

    const std::filesystem::path target = request.parent / request.name;
    if (std::filesystem::exists(target, ec)) {
        // Refused rather than merged. Writing a template over somebody's
        // existing folder is the one mistake here that costs work.
        result.error = core::makeError(LUAUG_TR("app.err.launcher_exists"), {}, target.string());
        return result;
    }

    if (!copyTemplate(source, target, request.name)) {
        result.error = core::makeError(LUAUG_TR("app.err.launcher_copy_failed"), {}, target.string());
        return result;
    }

    // `.luaug/` is generated and gitignored, which is why the definitions are
    // written here rather than kept in the template: they belong to the engine
    // version, not to the template. Same reasoning as `luaug new`'s.
    std::string engineDefinitions;
    if (!definitions.empty() && platform::readTextFile(definitions, engineDefinitions)) {
        const std::filesystem::path types = target / ".luaug" / "types";
        if (!platform::createDirectories(types) ||
            !platform::writeTextFile(types / "engine.d.luau", engineDefinitions)) {
            result.error = core::makeError(LUAUG_TR("app.err.launcher_copy_failed"), {}, target.string());
            return result;
        }
    }

    result.path = target;
    return result;
}

} // namespace luaug::app
