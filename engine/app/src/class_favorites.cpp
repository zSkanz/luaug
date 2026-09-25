#include "luaug/app/class_favorites.h"

#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"

#include <algorithm>

namespace luaug::app {

bool ClassFavorites::has(std::string_view parent, std::string_view child) const
{
    const std::span<const std::string> starred = of(parent);
    return std::binary_search(starred.begin(), starred.end(), child, std::less<>{});
}

std::span<const std::string> ClassFavorites::of(std::string_view parent) const
{
    const auto found = byParent.find(parent);
    return found != byParent.end() ? std::span<const std::string>(found->second) : std::span<const std::string>{};
}

bool ClassFavorites::toggle(std::string_view parent, std::string_view child)
{
    auto found = byParent.find(parent);
    if (found == byParent.end())
        found = byParent.emplace(std::string(parent), std::vector<std::string>{}).first;
    std::vector<std::string>& starred = found->second;
    const auto at = std::lower_bound(starred.begin(), starred.end(), child, std::less<>{});
    if (at != starred.end() && *at == child) {
        starred.erase(at);
        // An empty list is no list, so the file never collects parents with
        // nothing under them.
        if (starred.empty())
            byParent.erase(found);
        return false;
    }
    starred.insert(at, std::string(child));
    return true;
}

ClassFavorites& classFavorites()
{
    static ClassFavorites favorites = loadClassFavorites(classFavoritesFile());
    return favorites;
}

std::filesystem::path classFavoritesFile()
{
    const std::filesystem::path& userDir = platform::paths().userDir;
    return userDir.empty() ? std::filesystem::path{} : userDir / "class-favorites.json";
}

ClassFavorites loadClassFavorites(const std::filesystem::path& file)
{
    ClassFavorites favorites;
    std::string text;
    if (file.empty() || !platform::readTextFile(file, text))
        return favorites;
    core::JsonDocument document;
    if (!document.parse(text).ok)
        return favorites;
    const core::JsonValue parents = document.root()["favorites"];
    if (parents.type() != core::JsonType::Object)
        return favorites;
    for (std::size_t index = 0; index < parents.size(); ++index) {
        const std::string_view parent = parents.keyAt(index);
        const core::JsonValue children = parents[parent];
        if (children.type() != core::JsonType::Array)
            continue;
        for (std::size_t child = 0; child < children.size(); ++child) {
            const core::JsonValue name = children.at(child);
            // Toggled only when absent, so a name written twice is one star.
            if (name.type() == core::JsonType::String && !favorites.has(parent, name.asString()))
                (void)favorites.toggle(parent, name.asString());
        }
    }
    return favorites;
}

bool saveClassFavorites(const std::filesystem::path& file, const ClassFavorites& favorites)
{
    if (file.empty())
        return false;
    core::JsonWriter writer;
    writer.beginObject();
    writer.key("favorites");
    writer.beginObject();
    for (const auto& [parent, children] : favorites.byParent) {
        writer.key(parent);
        writer.beginArray();
        for (const std::string& child : children)
            writer.value(std::string_view(child));
        writer.endArray();
    }
    writer.endObject();
    writer.endObject();
    (void)platform::createDirectories(file.parent_path());
    return platform::writeTextFile(file, writer.text());
}

} // namespace luaug::app
