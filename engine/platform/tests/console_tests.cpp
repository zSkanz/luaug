#include <cstdio>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "luaug/platform/console.h"

using luaug::platform::ConsoleStream;
using luaug::platform::writeConsole;

namespace {

// MSVC rejects the plain CRT forms under warnings-as-errors, so both are
// wrapped once here rather than relaxing the bar for the whole target.
std::FILE* openFile(const char* path, const char* mode)
{
#ifdef _WIN32
    std::FILE* file = nullptr;
    return fopen_s(&file, path, mode) == 0 ? file : nullptr;
#else
    return std::fopen(path, mode);
#endif
}

bool redirectStdout(const char* path)
{
#ifdef _WIN32
    std::FILE* file = nullptr;
    return freopen_s(&file, path, "w", stdout) == 0;
#else
    return std::freopen(path, "w", stdout) != nullptr;
#endif
}

int duplicateStdoutFd()
{
#ifdef _WIN32
    return _dup(_fileno(stdout));
#else
    return dup(fileno(stdout));
#endif
}

void restoreStdoutFd(int saved)
{
#ifdef _WIN32
    _dup2(saved, _fileno(stdout));
    _close(saved);
#else
    dup2(saved, fileno(stdout));
    close(saved);
#endif
}

// Points stdout at a file for the duration of `body` and returns what landed
// there, then puts the original descriptor back so the test runner keeps its
// own output. stdout is never fclosed -- only its descriptor is swapped --
// because closing it would take doctest's reporting down with it.
//
// This exercises the redirected path on purpose. It is the one CI, CTest and
// every pipe take, and the console path is unreachable from a harness by
// definition (M0 Finding 12) -- which is exactly why it broke unnoticed once.
std::string captureStdout(void (*body)())
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "luaug_console_test.txt";
    const std::string pathText = path.string();

    std::fflush(stdout);
    const int saved = duplicateStdoutFd();
    REQUIRE(saved >= 0);

    REQUIRE(redirectStdout(pathText.c_str()));
    body();
    std::fflush(stdout);

    restoreStdoutFd(saved);
    std::clearerr(stdout);

    std::string captured;
    if (std::FILE* file = openFile(pathText.c_str(), "rb"); file != nullptr) {
        char buffer[256];
        while (const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file))
            captured.append(buffer, read);
        std::fclose(file);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return captured;
}

} // namespace

TEST_CASE("a redirected stream receives the UTF-8 bytes unchanged")
{
    // The em dash from engine.boot.hello: three bytes a Windows console on a
    // legacy codepage renders as "ÔÇö", and the reason this function exists.
    const std::string captured = captureStdout([] { writeConsole(ConsoleStream::Out, "LuauG \xE2\x80\x94 ok"); });

    CHECK(captured == "LuauG \xE2\x80\x94 ok");
}

TEST_CASE("writing nothing writes nothing")
{
    const std::string captured = captureStdout([] { writeConsole(ConsoleStream::Out, {}); });

    CHECK(captured.empty());
}
