#include "luaug/core/log.h"

#include <cstdio>
#ifdef _WIN32
#include <share.h>
#endif
#include <string>
#include <utility>

namespace luaug::core
{
namespace
{

LogSink& sinkSlot()
{
    static LogSink sink;
    return sink;
}

std::FILE*& fileSlot() noexcept
{
    static std::FILE* file = nullptr;
    return file;
}

// Flushed per line, deliberately. The whole reason this file exists is to
// survive a process that dies without unwinding, and a buffer is exactly what
// such a process does not get to flush. It costs a write syscall per line on a
// path that already pays one for the console.
void writeFile(LogLevel level, std::string_view text)
{
    std::FILE* file = fileSlot();
    if (file == nullptr)
        return;
    const std::string line = formatLogLine(level, text);
    std::fwrite(line.data(), 1, line.size(), file);
    std::fflush(file);
}

void writeDefault(LogLevel level, std::string_view text)
{
    // Warnings and errors go to stderr so a headless CI run can separate them
    // from ordinary output without parsing.
    std::FILE* stream = (level == LogLevel::Warn || level == LogLevel::Error) ? stderr : stdout;
    const std::string line = formatLogLine(level, text);
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fflush(stream);
}

} // namespace

std::string_view logLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    }
    return "info";
}

std::string formatLogLine(LogLevel level, std::string_view text)
{
    const std::string_view name = logLevelName(level);

    std::string line;
    line.reserve(name.size() + text.size() + 4);
    line += '[';
    line += name;
    line += "] ";
    line += text;
    line += '\n';
    return line;
}

LogSink setLogSink(LogSink sink)
{
    LogSink previous = std::move(sinkSlot());
    sinkSlot() = std::move(sink);
    return previous;
}

void resetLogSink()
{
    sinkSlot() = nullptr;
}

bool openLogFile(const std::filesystem::path& path)
{
    closeLogFile();
#ifdef _WIN32
    // `_wfsopen` with `_SH_DENYWR` rather than `fopen`/`_wfopen_s`, which open
    // for EXCLUSIVE access on this CRT: nothing else could read the file while
    // the engine was running, so tailing a live log -- which is most of why a
    // person wants one -- would fail with a sharing violation. Writers are still
    // denied, because two processes interleaving lines into one log is worse
    // than no log.
    std::FILE* file = ::_wfsopen(path.c_str(), L"wb", _SH_DENYWR);
#else
    std::FILE* file = std::fopen(path.c_str(), "wb");
#endif
    if (file == nullptr)
        return false;
    fileSlot() = file;
    return true;
}

void closeLogFile() noexcept
{
    std::FILE*& file = fileSlot();
    if (file != nullptr)
    {
        std::fclose(file);
        file = nullptr;
    }
}

void logText(LogLevel level, std::string_view text)
{
    // Before the sink rather than after, and outside the branch: a host that
    // installs a sink -- the DebugShell's log pane, a test capturing output --
    // must not be able to take the file away with it.
    writeFile(level, text);

    if (const LogSink& sink = sinkSlot())
        sink(level, text);
    else
        writeDefault(level, text);
}

void log(LogLevel level, TextKey key, std::span<const I18nArg> args)
{
    logText(level, engineCatalog().format(key, args));
}

} // namespace luaug::core
