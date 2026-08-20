// The pieces every `app` test that needs a real project on disk shares.
//
// Extracted when the reload tests wanted the same fixture: a second copy would
// have drifted from this one the first time `WorldHostOptions` grew a field,
// and the compiler would not have said so.
#pragma once

#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app::testing {

// A project on disk, because that is what the host mounts and a fake filesystem
// would be testing the fake. Removed on the way out, so a failed run leaves
// nothing behind for the next one to inherit.
struct Project
{
    std::filesystem::path root;

    Project()
    {
        static int counter = 0;
        root = std::filesystem::temp_directory_path() / ("luaug-worldhost-" + std::to_string(++counter) + "-" +
                                                         std::to_string(std::hash<std::string>{}(__FILE__)));
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

    // Errors are kept apart from the rest because a script error is CONTAINED
    // by design (api-design.md §3.1): it goes to the log and never to a return
    // value. A test that cannot see one passes for everything the script did
    // not get to do -- M2 Finding 14, which cost that milestone a whole signal
    // suite that was green and meaningless.
    std::vector<std::string> errors;

    Captured()
    {
        core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        core::setLogSink([this](core::LogLevel level, std::string_view text) {
            lines.emplace_back(text);
            if (level == core::LogLevel::Error)
                errors.emplace_back(text);
        });
    }
    ~Captured() { core::resetLogSink(); }

    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;

    // The first error a script logged, or empty. Assert on this after booting
    // anything whose script is supposed to run cleanly.
    [[nodiscard]] std::string firstError() const { return errors.empty() ? std::string{} : errors.front(); }

    [[nodiscard]] bool contains(std::string_view needle) const
    {
        for (const std::string& line : lines) {
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
// Whether `workspace` has a child of this name. The scripts in these tests
// encode what they observed into a folder name, because a name is the cheapest
// thing a script can say that a test can read without a second channel.
[[nodiscard]] inline bool hasChildNamed(luaug::app::WorldHost& host, std::string_view name)
{
    const luaug::core::NameAtom atom = host.world().atoms().lookup(name);
    return host.world().findFirstChild(host.workspace(), atom).valid();
}

[[nodiscard]] inline luaug::app::WorldHostOptions bootOptions(const std::filesystem::path& path, core::u64 seed = 1)
{
    return luaug::app::WorldHostOptions{
        .projectPath = path,
        .seed = seed,
        .fixedTimestep = 1.0 / 60.0,
        .reloadState = nullptr,
        .isReload = false,
        .preserved = nullptr,
        .conformanceRoot = {},
    };
}

} // namespace luaug::app::testing
