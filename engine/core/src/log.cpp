#include "luaug/core/log.h"

#include <cstdio>
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

void setLogSink(LogSink sink)
{
    sinkSlot() = std::move(sink);
}

void resetLogSink()
{
    sinkSlot() = nullptr;
}

void logText(LogLevel level, std::string_view text)
{
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
