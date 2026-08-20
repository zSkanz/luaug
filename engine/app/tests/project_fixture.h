// The pieces every `app` test that needs a real project on disk shares.
//
// Extracted when the reload tests wanted the same fixture: a second copy would
// have drifted from this one the first time `WorldHostOptions` grew a field,
// and the compiler would not have said so.
#pragma once

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"

namespace luaug::app::testing
{


// A project on disk, because that is what the host mounts and a fake filesystem
// would be testing the fake. Removed on the way out, so a failed run leaves
// nothing behind for the next one to inherit.
struct Project
{
    std::filesystem::path root;

    Project()
    {
        static int counter = 0;
        root = std::filesystem::temp_directory_path()
            / ("luaug-worldhost-" + std::to_string(++counter) + "-" + std::to_string(std::hash<std::string>{}(__FILE__)));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~Project()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    void write(std::string_view relative, std::string_view contents) const
    {
        const std::filesystem::path path = root / std::filesystem::path(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
};

struct Captured
{
    std::vector<std::string> lines;

    Captured()
    {
        core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        core::setLogSink([this](core::LogLevel, std::string_view text) { lines.emplace_back(text); });
    }
    ~Captured() { core::resetLogSink(); }

    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;

    [[nodiscard]] bool contains(std::string_view needle) const
    {
        for (const std::string& line : lines)
        {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }
};

// Every field named, because Clang's -Wmissing-field-initializers fires on a
// designated initializer that omits a trailing field even when that field has a
// default member initializer -- and warnings are errors here. One helper rather
// than fourteen braces also means the next option `WorldHostOptions` grows is
// one edit instead of fourteen.
[[nodiscard]] inline luaug::app::WorldHostOptions bootOptions(const std::filesystem::path& path, core::u64 seed = 1)
{
    return luaug::app::WorldHostOptions{
        .projectPath = path,
        .seed = seed,
        .fixedTimestep = 1.0 / 60.0,
        .conformanceRoot = {},
    };
}

} // namespace luaug::app::testing
