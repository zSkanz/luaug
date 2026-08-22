// Reading and writing a file the way the *platform* stores it (architecture.md
// §2). Reading is for content; writing is for the HOST alone -- see the
// "Writing" block below, which is where R4 is argued.
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::platform {

// Whole-file read. `out` is replaced on success and left untouched otherwise.
[[nodiscard]] bool readFile(const std::filesystem::path& path, std::vector<std::byte>& out);

// The same read, delivered as bytes in a std::string for JSON and other text.
// No transcoding and no newline translation: the bytes are the file's.
[[nodiscard]] bool readTextFile(const std::filesystem::path& path, std::string& out);

// Whether a file can be opened for reading.
//
// **Opens and closes rather than stats**, for the reason `readFile` exists at
// all: inside an APK the content directory is a set of zip entries and no path
// any C runtime can stat, so existence has to mean "can it be opened" and SDL
// is what knows how. On every other platform this is a stat behind an open.
//
// It exists because the alternative was worse (D039): `ContentMounts::resolve`
// READ THE WHOLE FILE to decide it existed, threw the bytes away, and then
// started an asynchronous read of the same file. On a slow filesystem that
// synchronous read was a frame hitch of tens of milliseconds inside the
// streaming pump, and it was doing twice the IO to get there.
[[nodiscard]] bool fileExists(const std::filesystem::path& path);

// --- Writing -----------------------------------------------------------------
//
// **The caller of everything below is the HOST -- the editor, the CLI, a
// tool -- and never a script.** None of it is registered as a native module
// in `engine/script/src/`, and none of it may be: `io` and `os.remove` are
// absent from the game VM by design (api-design.md §1.1) and R4 says the
// sandbox is never weakened. A game that could write a file could write the
// next file the host loads.
//
// Writes never land inside an APK -- a package is read-only, and what a game
// saves goes to app-private storage -- so the APK argument that put `readFile`
// here does not by itself apply. What does apply is the other half of the same
// reason: the write seam is the one place that knows how a path becomes bytes
// on this tier, and keeping both directions behind it means a platform whose
// writable storage is not a C-runtime path (a sandboxed container URL, a SAF
// document tree) is one file to change rather than every caller.

// Whole-file write, replacing whatever was there. Returns false and leaves the
// existing file untouched on any failure.
//
// **Atomic: the bytes go to a temporary file beside the target and are then
// RENAMED over it**, because the alternative loses data. Opening the target
// itself truncates it, so an interruption between the truncate and the last
// byte -- a crash, a kill, a full disk -- leaves a half-written scene where a
// good one was, and the good one is gone. A rename is a single directory
// operation on every tier this engine ships to (MoveFileExW with
// MOVEFILE_REPLACE_EXISTING on Windows, rename(2) elsewhere): a reader sees
// either the whole old file or the whole new one, and a write that dies partway
// leaves only a temporary to collect.
//
// The guarantee is against an interrupted PROCESS, not against a power cut: the
// bytes are handed to the OS, and SDL exposes no fsync to force them to the
// platter before the rename.
//
// The temporary is created in the target's own directory, never in the system
// temporary directory, because a rename is only atomic within one filesystem --
// across volumes Windows degrades it into a copy, which is exactly the
// non-atomic write this exists to avoid.
//
// The parent directory must already exist; this does not create it. A typo'd
// path should fail rather than silently scatter directories -- call
// `createDirectories` first, deliberately.
[[nodiscard]] bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes);

// The same write, taking text as bytes. No transcoding and no newline
// translation: what is passed is what lands on disk, which is what keeps a
// scene file recorded on one platform byte-identical on another.
[[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text);

// Creates `path` and every missing parent. True if it already exists as a
// directory; false if it exists as a FILE, which is a caller's mistake and not
// a state to write into.
//
// It is here rather than left to `std::filesystem::create_directories` --
// which `engine/app/src/engine.cpp` calls directly before writing a capture,
// and which is not wrong -- because it is half of ONE operation with
// `writeFile`. A save that made its directory with `std::filesystem` and wrote
// its bytes through this seam is split across two path conventions, and the day
// a tier's writable storage stops being a path both of them understand, only
// one of the two halves moves. A save is atomic about its bytes; it should be
// single-seamed about its location too.
//
// The scope is deliberately just that. For anything that is not preparing a
// write target -- enumerating a content tree the way `bench.cpp` does, testing
// for a directory, removing a build output -- `std::filesystem` remains the
// right answer, and wrapping it here would add surface without adding a
// capability.
[[nodiscard]] bool createDirectories(const std::filesystem::path& path);

} // namespace luaug::platform
