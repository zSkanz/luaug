// Reading a content file the way the *platform* stores it (architecture.md §2).
//
// `std::filesystem` plus `fopen` is not a portable content reader. Inside an
// Android APK the content directory is a directory of zip entries in the
// package, and there is no path any C runtime can open -- which is the reason
// this header exists rather than each module keeping its own `fopen` helper.
//
// SDL already owns that translation: an SDL_IOStream opened on a *relative*
// path is an AAssetManager entry on Android and an ordinary file everywhere
// else, and it opens absolute paths with the native API on every desktop tier.
// ADR 0004 makes this module and the SDL GPU backend the only places allowed to
// see SDL, so the seam belongs here and callers above it stay SDL-free (R17).
//
// Paths reach SDL as UTF-8 (`path::u8string()`), never as the narrow native
// encoding. On Windows SDL converts UTF-8 back to wide and calls CreateFileW,
// so a project under an accented directory survives the round trip;
// `path::string()` would hand MSVC's ANSI code page a string it cannot encode
// and lose the bytes.
//
// Failure is a bool rather than an EngineError on purpose: "the file was not
// readable" means something different to each caller -- a missing shader blob
// is a deployment fault, a missing project file is a user mistake -- and only
// the caller knows which key to raise.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace luaug::platform {

// Whole-file read. `out` is replaced on success and left untouched otherwise.
[[nodiscard]] bool readFile(const std::filesystem::path& path, std::vector<std::byte>& out);

// The same read, delivered as bytes in a std::string for JSON and other text.
// No transcoding and no newline translation: the bytes are the file's.
[[nodiscard]] bool readTextFile(const std::filesystem::path& path, std::string& out);

} // namespace luaug::platform
