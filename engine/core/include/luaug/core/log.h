// Engine logging. Every engine-originated message is named by a TextKey and
// formatted through the catalog (rule R3) -- there is no overload taking an
// engine-authored string, because that is exactly the hole R3 exists to close.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/core/types.h"

namespace luaug::core
{

enum class LogLevel : u8
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
};

[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

// Receives already-formatted text. Installed by the host so tests and the
// future DebugShell can capture output.
using LogSink = std::function<void(LogLevel, std::string_view)>;

// The `[level] text\n` line the built-in writer emits. Exposed so a sink that
// replaces where output goes -- not what it looks like -- does not have to
// re-derive the format and drift from it.
[[nodiscard]] std::string formatLogLine(LogLevel level, std::string_view text);

// Returns the sink being replaced, so a caller that needs to observe the stream
// without owning it -- the replay harness counting errors, a test capturing
// output -- can wrap what was there and put it back. Without this, "install a
// sink temporarily" means "silence whatever the host installed".
LogSink setLogSink(LogSink sink);
void resetLogSink();

void log(LogLevel level, TextKey key, std::span<const I18nArg> args = {});

// Verbatim passthrough for text that did NOT originate in the engine: script
// `print`/`error` output and other user-authored strings, which must not be
// translated (architecture.md §5). Engine code must use log() instead -- the
// i18n lint treats a literal reaching this from engine sources as a violation.
void logText(LogLevel level, std::string_view text);

} // namespace luaug::core
