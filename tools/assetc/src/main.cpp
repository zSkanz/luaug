// `assetc` -- argv plumbing and exit codes. The work is in the library beside
// it, so the tests exercise the same code the binary runs (the `imgcmp` shape).
#include "luaug/assetc/compiler.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/platform.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

namespace {

using luaug::assetc::CompileOptions;
using luaug::assetc::CompileResult;

// This tool's own console output is developer-facing diagnostics rather than
// engine messages, so it is printed directly; every ENGINE error it relays
// arrives already key-prefixed through `core::makeError` and stays that way
// (R3). The catalog is loaded beside the binary for exactly that reason -- a
// relayed error should read as prose rather than as a bare key.
void usage()
{
    std::cout << "usage: assetc --input <content-dir> --output <pack> --manifest <json> [--jobs N]\n"
                 "\n"
                 "  Compiles a content directory into one .lpack and its manifest.\n"
                 "  Deterministic: the same inputs produce the same bytes, and --jobs 1 is how\n"
                 "  that is CHECKED rather than asserted: the same tree built serially and in\n"
                 "  parallel must come out identical.\n";
}

[[nodiscard]] bool flagValue(int argc, char** argv, int& index, const char* name, std::string& out)
{
    if (std::strcmp(argv[index], name) != 0) {
        return false;
    }
    if (index + 1 >= argc) {
        std::cout << "assetc: " << name << " needs a value\n";
        return false;
    }
    out = argv[++index];
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string input;
    std::string output;
    std::string manifest;
    std::string jobsArgument;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
        if (flagValue(argc, argv, i, "--input", input) || flagValue(argc, argv, i, "--output", output) ||
            flagValue(argc, argv, i, "--manifest", manifest) || flagValue(argc, argv, i, "--jobs", jobsArgument)) {
            continue;
        }
        std::cout << "assetc: unknown option " << argv[i] << "\n";
        usage();
        return 2;
    }

    if (input.empty() || output.empty() || manifest.empty()) {
        usage();
        return 2;
    }

    // **The pool, and the flag that turns it off** (E9 step 11).
    //
    // Textures are encoded one per worker while basis's own threading stays off
    // -- `texture.cpp` says why that distinction matters -- and the merge is in
    // source order, so the pack this produces is byte-identical however many
    // workers ran. `--jobs=1` is how that claim is CHECKED rather than asserted:
    // `tests/assetdeterminism` builds the same tree twice, once serial and once
    // parallel, and requires the same bytes. A flag nobody can pass would leave
    // the determinism argument as prose.
    //
    // Zero is "as many as this machine has", which is `jobs::init`'s own default.
    if (jobsArgument == "1") {
        // Not initialising at all IS the serial mode: an uninitialised pool runs
        // every callable on the calling thread. Saying so here rather than
        // passing 1 keeps the two paths one path.
    }
    else {
        luaug::jobs::init(jobsArgument.empty() ? 0u : static_cast<luaug::core::u32>(std::stoul(jobsArgument)));
    }

    // Best effort: a missing catalog degrades a relayed engine error to its
    // key, which is still identifiable. It must not stop a build.
    (void)luaug::core::engineCatalog().loadFromFile(
        (luaug::platform::paths().contentDir / "i18n" / "en.json").string());

    CompileOptions options;
    options.inputRoot = input;

    const CompileResult result = luaug::assetc::compile(options);

    // **Joined before anything else.** A pool left running when a process
    // returns from `main` has its workers alive while static destructors run,
    // and this one died with 0xC0000409 and no output at all -- which reads as a
    // crash in the compile and is a crash in the exit.
    luaug::jobs::shutdown();
    if (!result.ok) {
        std::cout << "assetc: " << result.diagnostic << "\n";
        return 1;
    }

    std::string diagnostic;
    if (!luaug::assetc::writeFile(output, result.pack, diagnostic)) {
        std::cout << "assetc: " << diagnostic << "\n";
        return 1;
    }
    const std::span<const std::byte> manifestBytes(reinterpret_cast<const std::byte*>(result.manifest.data()),
                                                   result.manifest.size());
    if (!luaug::assetc::writeFile(manifest, manifestBytes, diagnostic)) {
        std::cout << "assetc: " << diagnostic << "\n";
        return 1;
    }

    // The chunks go beside the pack rather than in it, and the directory is
    // derived rather than asked for: `asset/chunk.h` explains why a chunk is
    // its own file, and a second flag naming where they land would be a second
    // way for the index and the files to disagree.
    if (result.chunkCount > 0) {
        const std::filesystem::path chunkRoot = std::filesystem::path(output).parent_path() / "content";
        for (const luaug::assetc::ChunkOutput& chunk : result.chunks) {
            if (!luaug::assetc::writeFile(chunkRoot / chunk.relativePath, chunk.bytes, diagnostic)) {
                std::cout << "assetc: " << diagnostic << "\n";
                return 1;
            }
        }

        const std::filesystem::path indexPath = std::filesystem::path(output).parent_path() / "content.chunks.json";
        const std::span<const std::byte> indexBytes(reinterpret_cast<const std::byte*>(result.chunkIndex.data()),
                                                    result.chunkIndex.size());
        if (!luaug::assetc::writeFile(indexPath, indexBytes, diagnostic)) {
            std::cout << "assetc: " << diagnostic << "\n";
            return 1;
        }
    }

    std::cout << "assetc: " << result.meshCount << " mesh(es), " << result.textureCount << " texture(s), "
              << result.chunkCount << " chunk(s), " << result.rawCount << " raw file(s) -> " << result.pack.size()
              << " bytes\n";
    return 0;
}
