#pragma once

// **The classes somebody stars in the add-a-child list** (the owner: "a star on
// the right, marking that one a favourite for that instance"). Kept per class
// of the PARENT -- a light starred under a `Part` is a favourite under every
// part -- because the list asks "what goes in this kind of thing", and a star
// that only held for one instance would be a star nobody saw twice.
//
// A person's, not a project's: `<userDir>/class-favorites.json`, like the
// script editor's settings. Favourites are how somebody works, and they carry
// from one project to the next.

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app {

struct ClassFavorites
{
    // Parent class name to the child class names starred under it, each list
    // in name order.
    std::map<std::string, std::vector<std::string>, std::less<>> byParent;

    [[nodiscard]] bool has(std::string_view parent, std::string_view child) const;
    [[nodiscard]] std::span<const std::string> of(std::string_view parent) const;
    // Stars it, or takes the star off. True when it is now a favourite.
    bool toggle(std::string_view parent, std::string_view child);
};

// The one the running editor reads, loaded from `classFavoritesFile()` the
// first time it is asked for. Main thread only, like the rest of the shell.
[[nodiscard]] ClassFavorites& classFavorites();

// `<userDir>/class-favorites.json`, or empty where there is no user directory.
[[nodiscard]] std::filesystem::path classFavoritesFile();
// A missing or unreadable file is no favourites, and a name this build does
// not know is kept: a class from another build is not a broken star.
[[nodiscard]] ClassFavorites loadClassFavorites(const std::filesystem::path& file);
[[nodiscard]] bool saveClassFavorites(const std::filesystem::path& file, const ClassFavorites& favorites);

} // namespace luaug::app
