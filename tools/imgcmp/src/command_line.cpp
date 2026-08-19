#include "luaug/imgcmp/command_line.h"

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace luaug::imgcmp
{
namespace
{

// A tolerance above the channel range would accept every possible image, which
// is a mistake worth catching at the command line rather than in a green gate.
constexpr int kMaxTolerance = 255;

template<typename Number>
[[nodiscard]] bool parseWholeNumber(std::string_view text, Number& out)
{
    if (text.empty())
        return false;

    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] Command usageError(std::string message)
{
    Command command;
    command.status = CommandStatus::UsageError;
    command.error = std::move(message);
    return command;
}

} // namespace

Command parseCommandLine(std::span<const std::string_view> args)
{
    Command command;
    std::vector<std::string_view> positional;

    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const std::string_view arg = args[index];

        if (arg == "-h" || arg == "--help")
        {
            command.status = CommandStatus::HelpRequested;
            return command;
        }

        // Anything else beginning with '-' is an option, never a path: silently
        // treating a misspelled `-tolerance` as a filename is how a comparator
        // ends up running with defaults nobody asked for.
        if (arg.size() > 1 && arg.front() == '-')
        {
            std::string_view name = arg;
            std::string_view value;
            bool hasValue = false;

            if (const std::size_t equals = arg.find('='); equals != std::string_view::npos)
            {
                name = arg.substr(0, equals);
                value = arg.substr(equals + 1);
                hasValue = true;
            }

            if (name != "--tolerance" && name != "--max-different-pixels" && name != "--diff")
                return usageError("unknown option \"" + std::string{name} + "\"");

            if (!hasValue)
            {
                if (index + 1 >= args.size())
                    return usageError(std::string{name} + " requires a value");
                value = args[++index];
            }

            if (name == "--diff")
            {
                if (value.empty())
                    return usageError("--diff requires a path");
                command.diffPath = std::string{value};
            }
            else if (name == "--tolerance")
            {
                int tolerance = 0;
                if (!parseWholeNumber(value, tolerance))
                    return usageError("--tolerance: \"" + std::string{value} + "\" is not a whole number");
                if (tolerance < 0 || tolerance > kMaxTolerance)
                    return usageError("--tolerance must be between 0 and 255");
                command.options.tolerance = tolerance;
            }
            else
            {
                std::size_t limit = 0;
                if (!parseWholeNumber(value, limit))
                    return usageError("--max-different-pixels: \"" + std::string{value} + "\" is not a whole number");
                command.options.maxDifferentPixels = limit;
            }

            continue;
        }

        positional.push_back(arg);
    }

    if (positional.empty())
        return usageError("missing <actual.png> and <expected.png>");
    if (positional.size() == 1)
        return usageError("missing <expected.png>");
    if (positional.size() > 2)
        return usageError("unexpected argument \"" + std::string{positional[2]} + "\"");

    command.actualPath = std::string{positional[0]};
    command.expectedPath = std::string{positional[1]};
    command.status = CommandStatus::Compare;
    return command;
}

std::string_view usageText() noexcept
{
    return R"(usage: imgcmp <actual.png> <expected.png> [options]

Compares two PNG images pixel by pixel and exits non-zero when they differ
beyond the allowed tolerance.

Options:
  --tolerance N              per-channel delta still counted as equal (default 2)
  --max-different-pixels N   differing pixels still counted as a match (default 0)
  --diff PATH                write a PNG highlighting the differing pixels
  -h, --help                 show this help

Exit codes:
  0  images match
  1  images differ
  2  usage or I/O error
)";
}

} // namespace luaug::imgcmp
