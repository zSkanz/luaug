#include <luaug/app/content_tree.h>

#include <algorithm>
#include <array>
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

    return ContentKind::Other;
}

bool ContentTree::isUsableName(std::string_view name) noexcept
{
    if (name.empty() || name == "." || name == "..")
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
    case ContentKind::Folder:
    case ContentKind::Mesh:
    case ContentKind::Texture:
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

} // namespace luaug::app
