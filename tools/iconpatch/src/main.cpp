// `iconpatch` -- put a game's icon into a built executable, and read it back.
//
// **Why this is a tool and not part of `luaug build`.** The CLI is a Lute
// application and Lute cannot call Win32; replacing an icon in an
// already-linked PE is `UpdateResourceW` and nothing else. So `luaug build`
// copies the host binary and runs this on the copy: no relink, no per-game
// build of the engine, and the packaged artifact wears the game's face rather
// than the engine's.
//
// **And why `verify` lives here too.** An icon is the most regression-prone
// thing in a build, because nothing fails when it is wrong: the program runs,
// the window opens, and it is wearing the wrong face. The roadmap asks for the
// check to read the resource back out of the built artifact rather than for
// somebody to look at it -- an icon nobody can assert is an icon that silently
// regresses.
//
// Windows only, deliberately. A Windows icon IS a PE resource; the macOS and
// Linux equivalents are a bundle's `Info.plist` and a `.desktop` entry, which
// are packaging steps for targets v1 does not build (roadmap M8).
//
//   iconpatch replace <executable> <icon.ico>
//   iconpatch verify  <executable> [--not <icon.ico>]
//
// Exit codes: 0 ok, 1 failed, 2 usage.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
// clang-format on
#endif

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;

// Deliberately not routed through the engine's catalog: this is a build tool a
// developer runs, like `imgcmp` and `assetc`, and it links nothing from
// `engine/`. R3 is about what a player sees.
void usage()
{
    std::fprintf(stderr, "usage: iconpatch replace <executable> <icon.ico>\n"
                         "       iconpatch verify  <executable> [--not <icon.ico>]\n");
}

[[nodiscard]] bool readFile(const std::string& path, std::vector<unsigned char>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamsize size = file.tellg();
    if (size <= 0)
        return false;
    file.seekg(0);
    out.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

// One entry of an `.ico` file's directory. The on-disk layout, which differs
// from the one a PE resource uses by exactly four bytes: a file entry ends with
// a 32-bit OFFSET into the file, and a group entry ends with a 16-bit ID naming
// another resource. That difference is the whole of the conversion below, and
// getting it wrong produces an executable with an icon-shaped hole in it.
struct IconEntry
{
    unsigned char width = 0;
    unsigned char height = 0;
    unsigned char colors = 0;
    unsigned char reserved = 0;
    unsigned short planes = 0;
    unsigned short bitCount = 0;
    unsigned int bytes = 0;
    unsigned int offset = 0;
};

[[nodiscard]] unsigned short readU16(const std::vector<unsigned char>& data, std::size_t at)
{
    return static_cast<unsigned short>(data[at] | (data[at + 1] << 8));
}

[[nodiscard]] unsigned int readU32(const std::vector<unsigned char>& data, std::size_t at)
{
    return static_cast<unsigned int>(data[at]) | (static_cast<unsigned int>(data[at + 1]) << 8) |
           (static_cast<unsigned int>(data[at + 2]) << 16) | (static_cast<unsigned int>(data[at + 3]) << 24);
}

// Parses an `.ico`. Empty on anything that is not one -- a truncated file, a
// cursor, a PNG somebody renamed -- because a build step that half-applied an
// icon is worse than one that refused.
[[nodiscard]] std::vector<IconEntry> parseIcon(const std::vector<unsigned char>& data)
{
    if (data.size() < 6 || readU16(data, 0) != 0 || readU16(data, 2) != 1)
        return {};

    const unsigned short count = readU16(data, 4);
    if (count == 0 || data.size() < 6u + static_cast<std::size_t>(count) * 16u)
        return {};

    std::vector<IconEntry> entries;
    entries.reserve(count);
    for (unsigned short index = 0; index < count; ++index) {
        const std::size_t at = 6u + static_cast<std::size_t>(index) * 16u;
        IconEntry entry;
        entry.width = data[at + 0];
        entry.height = data[at + 1];
        entry.colors = data[at + 2];
        entry.reserved = data[at + 3];
        entry.planes = readU16(data, at + 4);
        entry.bitCount = readU16(data, at + 6);
        entry.bytes = readU32(data, at + 8);
        entry.offset = readU32(data, at + 12);

        if (entry.bytes == 0 || static_cast<std::size_t>(entry.offset) + entry.bytes > data.size())
            return {};
        entries.push_back(entry);
    }
    return entries;
}

#if defined(_WIN32)

[[nodiscard]] std::wstring widen(const std::string& text)
{
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(size - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    return wide;
}

// The group directory a PE carries: the same header the file has, followed by
// 14-byte entries that name resource ids instead of file offsets.
[[nodiscard]] std::vector<unsigned char> buildGroup(const std::vector<IconEntry>& entries, unsigned short firstId)
{
    std::vector<unsigned char> group(6u + entries.size() * 14u, 0);
    group[2] = 1; // type: icon
    group[4] = static_cast<unsigned char>(entries.size() & 0xFF);
    group[5] = static_cast<unsigned char>((entries.size() >> 8) & 0xFF);

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const IconEntry& entry = entries[index];
        unsigned char* out = group.data() + 6u + index * 14u;
        out[0] = entry.width;
        out[1] = entry.height;
        out[2] = entry.colors;
        out[3] = entry.reserved;
        out[4] = static_cast<unsigned char>(entry.planes & 0xFF);
        out[5] = static_cast<unsigned char>((entry.planes >> 8) & 0xFF);
        out[6] = static_cast<unsigned char>(entry.bitCount & 0xFF);
        out[7] = static_cast<unsigned char>((entry.bitCount >> 8) & 0xFF);
        out[8] = static_cast<unsigned char>(entry.bytes & 0xFF);
        out[9] = static_cast<unsigned char>((entry.bytes >> 8) & 0xFF);
        out[10] = static_cast<unsigned char>((entry.bytes >> 16) & 0xFF);
        out[11] = static_cast<unsigned char>((entry.bytes >> 24) & 0xFF);
        const unsigned short id = static_cast<unsigned short>(firstId + index);
        out[12] = static_cast<unsigned char>(id & 0xFF);
        out[13] = static_cast<unsigned char>((id >> 8) & 0xFF);
    }
    return group;
}

// The icon this executable's group resource points at, largest first. Used by
// `verify` on a file that is not this process, which is why it goes through
// `LOAD_LIBRARY_AS_DATAFILE`: the loader maps the image and its resource table
// without running a byte of it.
[[nodiscard]] bool readLargestIcon(const std::string& path, std::vector<unsigned char>& out)
{
    const HMODULE module = ::LoadLibraryExW(widen(path).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module == nullptr)
        return false;

    bool ok = false;
    const HRSRC groupHandle = ::FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14 /* RT_GROUP_ICON */));
    if (groupHandle != nullptr) {
        const HGLOBAL group = ::LoadResource(module, groupHandle);
        const auto* directory = group == nullptr ? nullptr : static_cast<const unsigned char*>(::LockResource(group));
        if (directory != nullptr) {
            const unsigned int count =
                static_cast<unsigned int>(directory[4]) | (static_cast<unsigned int>(directory[5]) << 8);
            unsigned int bestId = 0;
            unsigned int bestPixels = 0;
            for (unsigned int index = 0; index < count; ++index) {
                const unsigned char* entry = directory + 6 + static_cast<std::size_t>(index) * 14u;
                // Zero means 256 in an icon directory.
                const unsigned int width = entry[0] == 0 ? 256u : entry[0];
                const unsigned int height = entry[1] == 0 ? 256u : entry[1];
                const unsigned int id =
                    static_cast<unsigned int>(entry[12]) | (static_cast<unsigned int>(entry[13]) << 8);
                if (width * height > bestPixels) {
                    bestPixels = width * height;
                    bestId = id;
                }
            }

            if (bestId != 0) {
                const HRSRC iconHandle =
                    ::FindResourceW(module, MAKEINTRESOURCEW(bestId), MAKEINTRESOURCEW(3 /* RT_ICON */));
                const DWORD size = iconHandle == nullptr ? 0 : ::SizeofResource(module, iconHandle);
                const HGLOBAL icon = iconHandle == nullptr ? nullptr : ::LoadResource(module, iconHandle);
                const auto* bytes = icon == nullptr ? nullptr : static_cast<const unsigned char*>(::LockResource(icon));
                if (bytes != nullptr && size > 0) {
                    out.assign(bytes, bytes + size);
                    ok = true;
                }
            }
        }
    }

    ::FreeLibrary(module);
    return ok;
}

[[nodiscard]] int replace(const std::string& executable, const std::string& iconPath)
{
    std::vector<unsigned char> iconFile;
    if (!readFile(iconPath, iconFile)) {
        std::fprintf(stderr, "iconpatch: cannot read %s\n", iconPath.c_str());
        return kExitFailed;
    }

    const std::vector<IconEntry> entries = parseIcon(iconFile);
    if (entries.empty()) {
        std::fprintf(stderr, "iconpatch: %s is not an icon file this tool can read\n", iconPath.c_str());
        return kExitFailed;
    }

    // `FALSE`: keep whatever else the binary carries. The host has only this one
    // resource today, and a tool that quietly deleted a manifest or a version
    // block because it was easier would be a trap for the milestone that adds
    // one.
    const HANDLE update = ::BeginUpdateResourceW(widen(executable).c_str(), FALSE);
    if (update == nullptr) {
        std::fprintf(stderr, "iconpatch: cannot open %s for resource update\n", executable.c_str());
        return kExitFailed;
    }

    constexpr unsigned short kFirstIconId = 1;
    constexpr WORD kNeutralLanguage = 0; // MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)

    bool ok = true;
    for (std::size_t index = 0; index < entries.size() && ok; ++index) {
        const IconEntry& entry = entries[index];
        ok = ::UpdateResourceW(update, MAKEINTRESOURCEW(3 /* RT_ICON */), MAKEINTRESOURCEW(kFirstIconId + index),
                               kNeutralLanguage, const_cast<unsigned char*>(iconFile.data() + entry.offset),
                               entry.bytes) != FALSE;
    }

    if (ok) {
        std::vector<unsigned char> group = buildGroup(entries, kFirstIconId);
        ok = ::UpdateResourceW(update, MAKEINTRESOURCEW(14 /* RT_GROUP_ICON */), MAKEINTRESOURCEW(1), kNeutralLanguage,
                               group.data(), static_cast<DWORD>(group.size())) != FALSE;
    }

    if (!::EndUpdateResourceW(update, ok ? FALSE : TRUE) || !ok) {
        std::fprintf(stderr, "iconpatch: writing the icon into %s failed\n", executable.c_str());
        return kExitFailed;
    }

    std::printf("iconpatch: %s now carries %zu icon size(s) from %s\n", executable.c_str(), entries.size(),
                iconPath.c_str());
    return kExitOk;
}

[[nodiscard]] int verify(const std::string& executable, const std::string& notIcon)
{
    std::vector<unsigned char> embedded;
    if (!readLargestIcon(executable, embedded)) {
        std::fprintf(stderr, "iconpatch: %s carries no icon resource\n", executable.c_str());
        return kExitFailed;
    }

    if (!notIcon.empty()) {
        // "Still the engine's default" is the failure this catches, and it is
        // the likely one: a packaging step that ran, reported success and
        // changed nothing produces an executable that works perfectly and
        // wears somebody else's face.
        std::vector<unsigned char> other;
        const std::vector<IconEntry> entries = readFile(notIcon, other) ? parseIcon(other) : std::vector<IconEntry>{};
        for (const IconEntry& entry : entries) {
            if (entry.bytes != embedded.size())
                continue;
            if (std::memcmp(other.data() + entry.offset, embedded.data(), embedded.size()) == 0) {
                std::fprintf(stderr, "iconpatch: %s still carries the icon from %s\n", executable.c_str(),
                             notIcon.c_str());
                return kExitFailed;
            }
        }
    }

    std::printf("iconpatch: %s carries an icon of %zu bytes\n", executable.c_str(), embedded.size());
    return kExitOk;
}

#endif // _WIN32

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index)
        args.emplace_back(argv[index]);

    if (args.size() < 2) {
        usage();
        return kExitUsage;
    }

#if !defined(_WIN32)
    std::fprintf(stderr, "iconpatch: Windows only; a Windows icon is a PE resource and this tool writes one\n");
    return kExitUsage;
#else
    if (args[0] == "replace") {
        if (args.size() != 3) {
            usage();
            return kExitUsage;
        }
        return replace(args[1], args[2]);
    }

    if (args[0] == "verify") {
        std::string notIcon;
        for (std::size_t index = 2; index < args.size(); ++index) {
            if (args[index] == "--not" && index + 1 < args.size()) {
                notIcon = args[index + 1];
                ++index;
                continue;
            }
            usage();
            return kExitUsage;
        }
        return verify(args[1], notIcon);
    }

    usage();
    return kExitUsage;
#endif
}
