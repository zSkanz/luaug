#include <luaug/app/content_tree.h>
#include <luaug/asset/material.h>
#include <luaug/core/json.h>
#include <luaug/platform/file.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

namespace luaug::app {
namespace {
[[nodiscard]] bool endsWith(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string out(text);
    // ASCII only, deliberately: an extension is ASCII, and a locale-aware
    // lowercase would make `content/TEXTURES` mean different things in Turkey
    // and everywhere else.
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// The class a stamp file is rooted at, read from the head of the file.
//
// **A prefix rather than a parse**, and the size is the argument: a stamp of a
// whole character is hundreds of kilobytes, a folder can hold forty of them, and
// this runs on every refresh to draw a row. The format is ours and the writer
// emits `class` as the root node's first field, so the answer is always in the
// first line -- and a file that does not have it there gets no answer rather
// than a wrong one, which costs a generic icon and nothing else.
[[nodiscard]] std::string rootClassOf(const std::filesystem::path& file)
{
    constexpr std::streamsize kHead = 512;
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return {};

    std::string head;
    head.resize(static_cast<std::size_t>(kHead));
    stream.read(head.data(), kHead);
    head.resize(static_cast<std::size_t>(stream.gcount()));

    const std::string::size_type at = head.find("\"root\"");
    if (at == std::string::npos)
        return {};
    const std::string::size_type key = head.find("\"class\"", at);
    if (key == std::string::npos)
        return {};
    const std::string::size_type open = head.find('"', head.find(':', key));
    if (open == std::string::npos)
        return {};
    const std::string::size_type close = head.find('"', open + 1);
    if (close == std::string::npos)
        return {};
    return head.substr(open + 1, close - open - 1);
}

[[nodiscard]] std::string joinRelative(std::string_view base, std::string_view name)
{
    if (base.empty())
        return std::string(name);
    return std::string(base) + "/" + std::string(name);
}
} // namespace

ContentKind contentKindOf(std::string_view fileName) noexcept
{
    const std::string name = lowered(fileName);

    // The specific suffix first. `.scene.json` also ends in `.json`, and asking
    // the short question first would call every scene a plain file.
    if (endsWith(name, kStampExtension))
        return ContentKind::Stamp;
    if (endsWith(name, kSceneExtension))
        return ContentKind::Scene;
    if (endsWith(name, ".chunk.json"))
        return ContentKind::Chunk;
    if (endsWith(name, asset::MaterialExtension))
        return ContentKind::Material;

    static constexpr std::array<std::string_view, 4> kMeshes{".glb", ".gltf", ".fbx", ".obj"};
    for (const std::string_view extension : kMeshes) {
        if (endsWith(name, extension))
            return ContentKind::Mesh;
    }

    static constexpr std::array<std::string_view, 5> kTextures{".png", ".jpg", ".jpeg", ".tga", ".ktx2"};
    for (const std::string_view extension : kTextures) {
        if (endsWith(name, extension))
            return ContentKind::Texture;
    }

    // The four `Sound.Content` documents it decodes, and the two faces
    // `TextLabel.Font` reads. Recorded here because the property pickers ask
    // this question -- what a `Sound` may be pointed at is what a person wants
    // listed when they click its `Content` -- and until they existed nothing
    // did.
    static constexpr std::array<std::string_view, 4> kAudio{".wav", ".mp3", ".flac", ".ogg"};
    for (const std::string_view extension : kAudio) {
        if (endsWith(name, extension))
            return ContentKind::Audio;
    }

    static constexpr std::array<std::string_view, 2> kFonts{".ttf", ".otf"};
    for (const std::string_view extension : kFonts) {
        if (endsWith(name, extension))
            return ContentKind::Font;
    }

    return ContentKind::Other;
}

bool ContentTree::isUsableName(std::string_view name) noexcept
{
    if (name.empty() || name == "." || name == "..")
        return false;
    // **Whitespace is not a name.** A folder called "   " is one nobody can
    // find, tell apart from its neighbour, or type again -- and on Windows a
    // trailing space is silently dropped, so the file ends up under a name the
    // person did not choose and the browser cannot match.
    if (name.find_first_not_of(" \t\r\n") == std::string_view::npos)
        return false;
    for (const char c : name) {
        // A separator would let a name climb out of the folder it was typed in,
        // which is the whole of the traversal problem in one character.
        if (c == '/' || c == '\\' || c == ':')
            return false;
        if (static_cast<unsigned char>(c) < 0x20)
            return false;
    }
    return true;
}

bool ContentTree::open(const std::filesystem::path& root, std::string_view relative)
{
    m_root = root;
    m_relative = std::string(relative);
    return refresh();
}

bool ContentTree::refresh()
{
    m_entries.clear();
    if (m_root.empty())
        return false;

    std::filesystem::path folder = m_root;
    if (!m_relative.empty())
        folder /= std::filesystem::path(m_relative);

    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec))
        return false;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || name.front() == '.')
            continue;

        ContentEntry row;
        row.name = name;
        row.path = joinRelative(m_relative, name);

        if (entry.is_directory(ec)) {
            row.kind = ContentKind::Folder;
        }
        else {
            row.kind = contentKindOf(name);
            const std::uintmax_t size = std::filesystem::file_size(entry.path(), ec);
            row.size = ec ? 0 : static_cast<core::u64>(size);
            ec.clear();
            // Read here rather than while drawing: `refresh` happens when
            // something changes and the panel draws sixty times a second.
            if (row.kind == ContentKind::Stamp)
                row.rootClass = rootClassOf(entry.path());
        }
        m_entries.push_back(std::move(row));
    }

    // Folders first, then names. Stable across machines, which the filesystem's
    // own order is not -- and a browser whose contents sit in different places
    // on two people's screens is one they cannot talk to each other about.
    std::sort(m_entries.begin(), m_entries.end(), [](const ContentEntry& a, const ContentEntry& b) {
        const bool aFolder = a.kind == ContentKind::Folder;
        const bool bFolder = b.kind == ContentKind::Folder;
        if (aFolder != bFolder)
            return aFolder;
        return lowered(a.name) < lowered(b.name);
    });
    return true;
}

bool ContentTree::enter(std::string_view folderName)
{
    if (!isUsableName(folderName))
        return false;

    const std::string next = joinRelative(m_relative, folderName);
    std::error_code ec;
    if (!std::filesystem::is_directory(m_root / std::filesystem::path(next), ec))
        return false;

    m_relative = next;
    return refresh();
}

bool ContentTree::leave()
{
    if (m_relative.empty())
        return false;

    const std::string::size_type cut = m_relative.rfind('/');
    m_relative = cut == std::string::npos ? std::string{} : m_relative.substr(0, cut);
    return refresh();
}

std::filesystem::path ContentTree::absolute(const ContentEntry& entry) const
{
    return m_root / std::filesystem::path(entry.path);
}

namespace {
// The suffix a kind carries, or empty. Kept beside `contentKindOf` so the two
// answers cannot drift: one decides what a name IS and this one decides what
// part of it says so.
[[nodiscard]] std::string_view extensionFor(ContentKind kind) noexcept
{
    switch (kind) {
    case ContentKind::Scene:
        return kSceneExtension;
    case ContentKind::Stamp:
        return kStampExtension;
    case ContentKind::Chunk:
        return ".chunk.json";
    case ContentKind::Material:
        return asset::MaterialExtension;
    case ContentKind::Folder:
    case ContentKind::Mesh:
    case ContentKind::Texture:
    case ContentKind::Audio:
    case ContentKind::Font:
    case ContentKind::Other:
        break;
    }
    return {};
}
} // namespace

std::string ContentTree::stemOf(const ContentEntry& entry)
{
    const std::string_view extension = extensionFor(entry.kind);
    if (!extension.empty() && entry.name.size() > extension.size() &&
        lowered(entry.name).compare(entry.name.size() - extension.size(), extension.size(), extension) == 0) {
        return entry.name.substr(0, entry.name.size() - extension.size());
    }

    // Anything else keeps whatever it has. A `.glb` renamed by its stem would
    // stop being a mesh, and this browser is not the place that decides a
    // texture is no longer one.
    const std::string::size_type dot = entry.name.rfind('.');
    return dot == std::string::npos || entry.kind == ContentKind::Folder ? entry.name : entry.name.substr(0, dot);
}

std::string ContentTree::displayNameOf(const ContentEntry& entry)
{
    constexpr std::string_view suffix = ".json";
    const std::string_view extension = extensionFor(entry.kind);
    // Only where the kind declares a COMPOUND suffix. `extensionFor` is the one
    // answer to "what part of this name says what it is", and asking it here is
    // what stops this from trimming a `.json` that is somebody's own data.
    if (extension.size() > suffix.size() && entry.name.size() > suffix.size() &&
        lowered(entry.name).compare(entry.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return entry.name.substr(0, entry.name.size() - suffix.size());
    }
    return entry.name;
}

bool ContentTree::rename(const ContentEntry& entry, std::string_view newName)
{
    if (!isUsableName(newName) || m_root.empty())
        return false;

    std::string target(newName);
    // The suffix is put back rather than required. Somebody typing a name is
    // not asking for the file to stop being what it is.
    if (const std::string_view extension = extensionFor(entry.kind);
        !extension.empty() &&
        (target.size() < extension.size() ||
         lowered(target).compare(target.size() - extension.size(), extension.size(), extension) != 0)) {
        target += std::string(extension);
    }
    else if (entry.kind != ContentKind::Folder && extensionFor(entry.kind).empty()) {
        // A mesh or a texture keeps the extension it arrived with.
        if (const std::string::size_type dot = entry.name.rfind('.'); dot != std::string::npos)
            target += entry.name.substr(dot);
    }

    if (target == entry.name)
        return true;

    std::filesystem::path folder = m_root;
    if (!m_relative.empty())
        folder /= std::filesystem::path(m_relative);

    std::error_code ec;
    // Refused rather than silently replacing. Two files with one name is a
    // question, and answering it by destroying one of them is not an answer.
    if (std::filesystem::exists(folder / std::filesystem::path(target), ec))
        return false;

    std::filesystem::rename(folder / std::filesystem::path(entry.name), folder / std::filesystem::path(target), ec);
    if (ec)
        return false;

    return refresh();
}

std::string ContentTree::createMaterial(std::string_view materialName)
{
    if (m_root.empty() || !isUsableName(materialName))
        return {};

    // The suffix is put back rather than required, exactly as `rename` does it:
    // typing `stone` means a material called stone, and typing
    // `stone.material.json` means the same thing.
    std::string target(materialName);
    const std::string loweredTarget = lowered(target);
    const std::string_view extension = asset::MaterialExtension;
    if (target.size() < extension.size() ||
        loweredTarget.compare(target.size() - extension.size(), extension.size(), extension) != 0) {
        target += std::string(extension);
    }

    std::filesystem::path folder = m_root;
    if (!m_relative.empty())
        folder /= std::filesystem::path(m_relative);

    std::error_code ec;
    const std::filesystem::path path = folder / std::filesystem::path(target);
    // Refused rather than replacing. Somebody who typed a name that is already
    // taken has made a mistake, and a browser that answered by destroying their
    // material is one they stop trusting with anything.
    if (std::filesystem::exists(path, ec))
        return {};

    if (!platform::createDirectories(folder))
        return {};
    // The default block: white, dielectric, no maps. A part pointed at it looks
    // exactly as it did with none, which is the starting point somebody wants --
    // change one field, see one change.
    asset::MaterialAsset made;
    // Named after the file, which is what somebody typed and what they will
    // look for. The two can drift later -- a material may be renamed inside
    // itself -- and this is only the starting point.
    made.name = target.substr(0, target.size() - extension.size());
    if (!platform::writeTextFile(path, asset::writeMaterial(made)))
        return {};

    if (!refresh())
        return {};
    return target;
}

std::string ContentTree::duplicate(const ContentEntry& entry)
{
    if (m_root.empty())
        return {};

    std::filesystem::path folder = m_root;
    if (!m_relative.empty())
        folder /= std::filesystem::path(m_relative);

    // The stem and the suffix, split by what the KIND says rather than at the
    // last dot: `stone.material.json` has a two-part extension, and a duplicate
    // called `stone.material 2.json` is a file the browser no longer recognises
    // as a material.
    const std::string stem = stemOf(entry);
    std::string suffix;
    if (const std::string_view extension = extensionFor(entry.kind); !extension.empty())
        suffix = std::string(extension);
    else if (entry.kind != ContentKind::Folder) {
        if (const std::string::size_type dot = entry.name.rfind('.'); dot != std::string::npos)
            suffix = entry.name.substr(dot);
    }

    std::error_code ec;
    std::string target;
    // First free, from two. A person duplicating three times gets 2, 3 and 4,
    // and one who deleted the 3 gets it back -- which is what "first free"
    // means and is less surprising than a counter that only ever climbs.
    for (int index = 2; index < 1000; ++index) {
        std::string candidate = stem + " " + std::to_string(index) + suffix;
        if (!std::filesystem::exists(folder / std::filesystem::path(candidate), ec)) {
            target = std::move(candidate);
            break;
        }
    }
    if (target.empty())
        return {};

    const std::filesystem::path source = folder / std::filesystem::path(entry.name);
    const std::filesystem::path destination = folder / std::filesystem::path(target);
    if (entry.kind == ContentKind::Folder) {
        std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, ec);
    }
    else {
        std::filesystem::copy_file(source, destination, ec);
    }
    if (ec)
        return {};

    if (!refresh())
        return {};
    return target;
}

bool ContentTree::remove(const ContentEntry& entry)
{
    if (m_root.empty())
        return false;

    std::error_code ec;
    // `remove_all` because a folder means the folder, and a delete that left
    // the contents behind would be a folder somebody cannot get rid of.
    const std::uintmax_t removed = std::filesystem::remove_all(absolute(entry), ec);
    if (ec || removed == 0)
        return false;

    return refresh();
}

bool ContentTree::createFolder(std::string_view folderName)
{
    if (!isUsableName(folderName) || m_root.empty())
        return false;

    std::filesystem::path target = m_root;
    if (!m_relative.empty())
        target /= std::filesystem::path(m_relative);
    target /= std::filesystem::path(folderName);

    std::error_code ec;
    // `create_directory` and not `create_directories`: the name has already been
    // refused a separator, so there is only ever one level to make, and the
    // recursive form would silently succeed on a path this rejected.
    if (!std::filesystem::create_directory(target, ec) || ec)
        return false;

    return refresh();
}

std::vector<std::string> ContentTree::filesOfKind(ContentKind kind) const
{
    std::vector<std::string> found;
    std::error_code ec;
    if (m_root.empty() || !std::filesystem::is_directory(m_root, ec))
        return found;

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(m_root, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec))
            continue;
        const std::string name = entry.path().filename().string();
        if (contentKindOf(name) != kind)
            continue;
        // Generic, because this becomes half of an `asset://` URI and those are
        // forward-slashed on every tier.
        found.push_back(entry.path().lexically_relative(m_root).generic_string());
    }

    std::sort(found.begin(), found.end());
    return found;
}

namespace {

// Per cent-encoding, undone. A glTF URI is a URI, so an exporter writes a space
// as `%20` and a filesystem does not know what that is.
[[nodiscard]] std::string decodeUri(std::string_view uri)
{
    std::string out;
    out.reserve(uri.size());
    for (core::usize index = 0; index < uri.size(); ++index) {
        if (uri[index] == '%' && index + 2 < uri.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            const int high = hex(uri[index + 1]);
            const int low = hex(uri[index + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>(high * 16 + low));
                index += 2;
                continue;
            }
        }
        out.push_back(uri[index]);
    }
    return out;
}

// The files a `.gltf` names, relative to it, in the order it names them.
//
// Read with the engine's own JSON parser rather than by importing the model:
// this runs when somebody drops a file into a folder, and parsing a fourteen
// megabyte buffer to find out where its buffer is would be reading the whole
// model to copy it.
//
// A `data:` URI carries its bytes inside the document and names no file, and a
// URI with a scheme is somebody's remote asset, which this is not going to
// fetch. Both are skipped rather than reported: neither is missing.
[[nodiscard]] std::vector<std::string> referencedFiles(const std::filesystem::path& gltf)
{
    std::vector<std::string> out;
    std::string text;
    if (!platform::readTextFile(gltf, text))
        return out;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text, "gltf"); !parsed.ok)
        return out;

    const auto collect = [&](std::string_view arrayName) {
        const core::JsonValue array = document.root()[arrayName];
        if (array.type() != core::JsonType::Array)
            return;
        for (core::usize index = 0; index < array.size(); ++index) {
            const std::string_view uri = array.at(index)["uri"].asString();
            if (uri.empty() || uri.starts_with("data:") || uri.find("://") != std::string_view::npos)
                continue;
            std::string decoded = decodeUri(uri);
            // The same buffer named twice is one file to copy.
            if (std::find(out.begin(), out.end(), decoded) == out.end())
                out.push_back(std::move(decoded));
        }
    };
    collect("buffers");
    collect("images");
    return out;
}

} // namespace

ContentTree::ImportReport ContentTree::import(std::span<const std::filesystem::path> sources)
{
    ImportReport report;
    if (m_root.empty())
        return report;

    std::filesystem::path folder = m_root;
    if (!m_relative.empty())
        folder /= std::filesystem::path(m_relative);

    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec))
        return report;

    for (const std::filesystem::path& source : sources) {
        const std::string name = source.filename().string();
        if (name.empty())
            continue;

        // A directory is a different request. Doing it by accident because
        // somebody multi-selected one is not an outcome to design for.
        if (std::filesystem::is_directory(source, ec)) {
            report.skipped.push_back(name);
            continue;
        }

        const std::filesystem::path target = folder / source.filename();
        if (std::filesystem::exists(target, ec)) {
            // **Refused rather than overwritten.** Replacing a file somebody has
            // already put work into is the one mistake here that costs work, and
            // the browser can say what it skipped.
            report.skipped.push_back(name);
            continue;
        }

        // Bytes, through `std::filesystem`: what is being copied is a mesh, a
        // texture or a sound as often as it is text, and a read-as-string round
        // trip would corrupt every one of those.
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, ec);
        if (ec) {
            ec.clear();
            report.failed.push_back(name);
            continue;
        }
        report.imported.push_back(name);

        // **And whatever it names.** See the header: a `.gltf` without its
        // buffer is a file that parses and loads nothing, and the person who
        // dragged it in chose a model rather than a manifest.
        // The extension, lowered: a `.GLTF` off a case-insensitive filesystem
        // is the same file.
        std::string suffix = source.extension().string();
        std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (suffix != ".gltf")
            continue;

        for (const std::string& relative : referencedFiles(source)) {
            const std::filesystem::path companionSource = source.parent_path() / std::filesystem::path(relative);
            if (!std::filesystem::is_regular_file(companionSource, ec)) {
                ec.clear();
                report.missing.push_back(relative);
                continue;
            }

            // Into the same relative place, because the URIs inside the file are
            // relative and rewriting them would be editing somebody's asset.
            const std::filesystem::path companionTarget = folder / std::filesystem::path(relative);
            if (std::filesystem::exists(companionTarget, ec)) {
                ec.clear();
                report.skipped.push_back(relative);
                continue;
            }
            std::filesystem::create_directories(companionTarget.parent_path(), ec);
            ec.clear();
            std::filesystem::copy_file(companionSource, companionTarget, std::filesystem::copy_options::none, ec);
            if (ec) {
                ec.clear();
                report.failed.push_back(relative);
                continue;
            }
            report.companions.push_back(relative);
        }
    }

    if (!report.imported.empty())
        (void)refresh();
    return report;
}

} // namespace luaug::app
