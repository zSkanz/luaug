#include "luaug/core/log.h"

#include <cstdio>
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
    std::fprintf(stream, "[%.*s] %.*s\n", static_cast<int>(logLevelName(level).size()), logLevelName(level).data(),
        static_cast<int>(text.size()), text.data());
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
