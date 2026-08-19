// luaug-host -- the M0 engine host: boot a sandboxed Luau VM, run one script,
// report failures as structured, key-identified engine errors.

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Luau/Bytecode.h>
#include <lua.h>

#include "luaug/app/script_host.h"
#include "luaug/core/build_info.h"
#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/platform/console.h"
#include "luaug/platform/platform.h"

namespace
{

using luaug::core::I18nArg;
using luaug::core::LogLevel;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;
constexpr int kExitScriptError = 1;
constexpr int kExitNoCatalog = 3;

// Catalogs are UTF-8 (ADR 0019); a Windows console decodes raw byte writes with
// its own codepage and mangles anything non-ASCII, which would quietly reduce
// "adding a locale is adding a file" to "adding a locale nobody on Windows can
// read". `platform::writeConsole` is the fix, and routing the log through it is
// how every engine message gets it.
void installConsoleLogSink()
{
    luaug::core::setLogSink(
        [](LogLevel level, std::string_view text)
        {
            // Warnings and errors go to stderr so a headless CI run can
            // separate them from ordinary output without parsing.
            const auto stream = (level == LogLevel::Warn || level == LogLevel::Error)
                ? luaug::platform::ConsoleStream::Err
                : luaug::platform::ConsoleStream::Out;
            luaug::platform::writeConsole(stream, luaug::core::formatLogLine(level, text));
        });
}

// The one place a user-facing string may be hardcoded: if the catalog itself
// failed to load there is, by definition, nothing to translate through. Kept
// deliberately to stderr and to this single call site (ADR 0019).
void reportCatalogFailure(const std::string& diagnostic)
{
    std::fprintf(stderr, "luaug-host: cannot load the message catalog: %s\n", diagnostic.c_str());
}

void printVersion()
{
    const std::array<I18nArg, 2> engineArgs{
        I18nArg{"version", LUAUG_VERSION_STRING}, I18nArg{"profile", LUAUG_PROFILE_NAME}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.engine"), engineArgs);

    // Version and commit come from third_party/manifest.json via the generated
    // provenance header (ADR 0031) -- Luau itself ships no version constant.
    const std::array<I18nArg, 2> luauArgs{
        I18nArg{"version", LUAUG_LUAU_VERSION}, I18nArg{"commit", LUAUG_LUAU_COMMIT}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.luau"), luauArgs);

    // These come from the vendored headers at compile time, so they describe
    // the VM actually linked into this binary rather than what we intended.
    const std::array<I18nArg, 4> abiArgs{
        I18nArg{"bytecode", static_cast<luaug::core::i64>(LBC_VERSION_TARGET)},
        I18nArg{"types", static_cast<luaug::core::i64>(LBC_TYPE_VERSION_TARGET)},
        I18nArg{"vectorSize", static_cast<luaug::core::i64>(LUA_VECTOR_SIZE)},
        I18nArg{"vectorPrecision", LUA_VECTOR_DOUBLE ? std::string_view{"f64"} : std::string_view{"f32"}}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.abi"), abiArgs);
}

} // namespace

int main(int argc, char** argv)
{
    installConsoleLogSink();

    const auto& paths = luaug::platform::paths();
    const auto catalogLoad = luaug::core::engineCatalog().loadFromFile(paths.contentDir / "i18n" / "en.json");
    if (!catalogLoad)
    {
        reportCatalogFailure(catalogLoad.diagnostic);
        return kExitNoCatalog;
    }

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);

    if (args.empty())
    {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.no_script"));
        return kExitUsage;
    }

    if (args[0] == "--version")
    {
        printVersion();
        return kExitOk;
    }

    if (args[0] == "--help")
    {
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.usage"));
        return kExitOk;
    }

    if (!args[0].empty() && args[0].front() == '-')
    {
        const std::array<I18nArg, 1> unknownArgs{I18nArg{"option", args[0]}};
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.unknown_option"), unknownArgs);
        return kExitUsage;
    }

    const std::array<I18nArg, 1> bootArgs{I18nArg{"version", LUAUG_VERSION_STRING}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.boot.hello"), bootArgs);

    luaug::app::ScriptHost host;
    if (const std::optional<luaug::core::EngineError> error = host.runFile(std::filesystem::path(args[0])))
    {
        luaug::core::logText(LogLevel::Error, error->message);
        if (!error->detail.empty())
            luaug::core::logText(LogLevel::Error, error->detail);
        return kExitScriptError;
    }

    return kExitOk;
}
