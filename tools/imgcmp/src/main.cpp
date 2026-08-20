#include "luaug/imgcmp/command_line.h"
#include "luaug/imgcmp/compare.h"
#include "luaug/imgcmp/image.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

// CI has to tell "the images differ" apart from "the screenshot was never
// written". Three codes, never overloaded.
constexpr int kExitMatch = 0;
constexpr int kExitDiffer = 1;
constexpr int kExitError = 2;

void reportError(std::string_view message)
{
    std::cerr << "imgcmp: " << message << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    using namespace luaug::imgcmp;

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);

    const Command command = parseCommandLine(args);
    if (command.status == CommandStatus::HelpRequested) {
        std::cout << usageText();
        return kExitMatch;
    }
    if (command.status == CommandStatus::UsageError) {
        reportError(command.error);
        std::cerr << usageText();
        return kExitError;
    }

    const LoadResult actual = loadPngFile(command.actualPath);
    if (!actual.ok) {
        reportError(actual.error);
        return kExitError;
    }

    const LoadResult expected = loadPngFile(command.expectedPath);
    if (!expected.ok) {
        reportError(expected.error);
        return kExitError;
    }

    const CompareResult result = compare(actual.image, expected.image, command.options);

    if (result.status == CompareStatus::SizeMismatch) {
        std::cout << "imgcmp: size mismatch: actual " << actual.image.width << "x" << actual.image.height
                  << ", expected " << expected.image.width << "x" << expected.image.height << ": FAIL\n";
        if (!command.diffPath.empty())
            reportError("--diff skipped: a diff image needs a common pixel grid");
        return kExitDiffer;
    }

    std::cout << "imgcmp: " << actual.image.width << "x" << actual.image.height << ", " << result.differentPixels
              << " differing pixel" << (result.differentPixels == 1 ? "" : "s") << " (max channel delta "
              << result.maxChannelDelta << ", tolerance " << command.options.tolerance << ", allowed "
              << command.options.maxDifferentPixels << "): " << (result.passed() ? "OK" : "FAIL") << '\n';

    if (!command.diffPath.empty()) {
        const Image diff = renderDiff(actual.image, expected.image, command.options);
        const WriteResult written = writePngFile(command.diffPath, diff);
        if (!written.ok) {
            // Failing to produce a requested artifact is an I/O failure even
            // when the comparison itself reached a verdict.
            reportError(written.error);
            return kExitError;
        }
    }

    return result.passed() ? kExitMatch : kExitDiffer;
}
