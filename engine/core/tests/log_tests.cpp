// The file sink `architecture.md` §app names, and one property that is the
// whole point of it: a host installs a sink to capture output for its own log
// pane, and the file must still receive every line. A file that went quiet
// exactly when something was watching would be a file nobody could rely on.
#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "luaug/core/log.h"

using namespace luaug;

namespace
{

struct TempFile
{
    std::filesystem::path path;

    TempFile()
    {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / ("luaug-log-" + std::to_string(++counter) + ".txt");
        std::filesystem::remove(path);
    }

    ~TempFile()
    {
        core::closeLogFile();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] std::string read() const
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

} // namespace

TEST_CASE("the log file receives lines even while a sink is installed")
{
    TempFile file;
    REQUIRE(core::openLogFile(file.path));

    std::vector<std::string> captured;
    core::LogSink previous =
        core::setLogSink([&captured](core::LogLevel, std::string_view text) { captured.emplace_back(text); });

    core::logText(core::LogLevel::Info, "first");
    core::logText(core::LogLevel::Error, "second");

    core::setLogSink(std::move(previous));
    core::closeLogFile();

    // Both, not either: the sink is where a host's log pane reads from, and the
    // file is what survives a process that dies without unwinding.
    REQUIRE(captured.size() == 2);
    const std::string contents = file.read();
    CHECK(contents.find("[info] first") != std::string::npos);
    CHECK(contents.find("[error] second") != std::string::npos);
}

TEST_CASE("each line is flushed, so a process that dies leaves what it wrote")
{
    // Read while the file is still open. Without the per-line flush this comes
    // back empty, which is precisely the failure the file exists to prevent --
    // and precisely what the human's captured crash log looked like.
    TempFile file;
    REQUIRE(core::openLogFile(file.path));
    core::logText(core::LogLevel::Warn, "written before anything closed it");
    CHECK(file.read().find("written before anything closed it") != std::string::npos);
    core::closeLogFile();
}

TEST_CASE("opening a second file replaces the first, and closing twice is safe")
{
    TempFile first;
    TempFile second;
    REQUIRE(core::openLogFile(first.path));
    core::logText(core::LogLevel::Info, "to the first");
    REQUIRE(core::openLogFile(second.path));
    core::logText(core::LogLevel::Info, "to the second");
    core::closeLogFile();
    core::closeLogFile();

    CHECK(first.read().find("to the first") != std::string::npos);
    CHECK(first.read().find("to the second") == std::string::npos);
    CHECK(second.read().find("to the second") != std::string::npos);
}

TEST_CASE("no file open is the ordinary case and costs nothing")
{
    core::closeLogFile();
    core::logText(core::LogLevel::Info, "nowhere in particular");
    CHECK(true);
}
