// What a crash handler CAN be unit-tested for, which is less than one would
// like: everything interesting about it happens after a fault, and a test that
// faults on purpose takes the test runner with it.
//
// So these cover the half that is testable and is also the half that failed in
// practice: the artifact path is built at INSTALL time, before anything is
// broken. A handler that formatted its path while handling a fault would be
// allocating in a process whose heap is one of the things that might be
// corrupt, and would produce nothing exactly when it is needed.
#include "luaug/platform/crash.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <ostream>
#include <string>

using namespace luaug;

TEST_CASE("the artifact path is resolved when the handler is installed")
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "luaug-crash-test";
    std::filesystem::create_directories(directory);

    REQUIRE(platform::installCrashHandler(directory));

    const std::filesystem::path artifact = platform::crashArtifactPath();
    CHECK(artifact.parent_path() == directory);
    CHECK_FALSE(artifact.filename().empty());
    // Named by process id, so two runs of the same binary do not overwrite each
    // other's evidence -- which matters most when a defect only reproduces
    // sometimes and the useful dump is the one from three runs ago.
    CHECK(artifact.filename().string().find("luaug-crash-") == 0);

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
}

TEST_CASE("installing twice replaces the directory rather than stacking")
{
    const std::filesystem::path first = std::filesystem::temp_directory_path() / "luaug-crash-a";
    const std::filesystem::path second = std::filesystem::temp_directory_path() / "luaug-crash-b";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);

    REQUIRE(platform::installCrashHandler(first));
    REQUIRE(platform::installCrashHandler(second));
    CHECK(platform::crashArtifactPath().parent_path() == second);

    std::error_code ec;
    std::filesystem::remove_all(first, ec);
    std::filesystem::remove_all(second, ec);
}
