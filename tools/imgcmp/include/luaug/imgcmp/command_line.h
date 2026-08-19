// The imgcmp command line.
//
// Parsing lives behind its own seam because a comparator whose --tolerance
// silently failed to apply is exactly the bug that turns a green gate into a
// lie. Behind a seam the flags are covered by unit tests instead of by hope.
#pragma once

#include <span>
#include <string>
#include <string_view>

#include "luaug/imgcmp/compare.h"

namespace luaug::imgcmp
{

enum class CommandStatus
{
    Compare,
    HelpRequested,
    UsageError,
};

struct Command
{
    CommandStatus status = CommandStatus::UsageError;
    std::string actualPath;
    std::string expectedPath;
    std::string diffPath; // empty unless --diff was given
    CompareOptions options;
    std::string error; // populated only for UsageError
};

// Parses the argument list with argv[0] already removed. Both `--flag value`
// and `--flag=value` are accepted.
[[nodiscard]] Command parseCommandLine(std::span<const std::string_view> args);

[[nodiscard]] std::string_view usageText() noexcept;

} // namespace luaug::imgcmp
