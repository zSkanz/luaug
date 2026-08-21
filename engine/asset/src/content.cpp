#include "luaug/asset/content.h"

#include "luaug/core/i18n.h"
#include "luaug/core/json.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"

#include <algorithm>

namespace luaug::asset {
namespace {

using core::I18nArg;

[[nodiscard]] AssetKind kindFromName(std::string_view name) noexcept
{
    if (name == "mesh") {
        return AssetKind::Mesh;
    }
    if (name == "texture") {
        return AssetKind::Texture;
    }
    if (name == "prefab") {
        return AssetKind::Prefab;
    }
    if (name == "chunk") {
        return AssetKind::Chunk;
    }
    if (name == "raw") {
        return AssetKind::Raw;
    }
    return AssetKind::Unknown;
}

} // namespace

bool isValidUrn(std::string_view urn)
{
    if (urn.size() <= AssetScheme.size() || urn.substr(0, AssetScheme.size()) != AssetScheme) {
        return false;
    }
    const std::string_view path = urn.substr(AssetScheme.size());
    if (path.empty() || path.front() == '/') {
        return false;
    }

    // A URN is a name inside the mount and never a way out of it. Refused here
    // rather than at the filesystem, because a shipped game resolving against a
    // pack has no filesystem to refuse it -- and because `..` inside a pack key
    // would silently never match, which reads as "missing asset" rather than as
    // "you wrote something that cannot work".
    if (path.find('\\') != std::string_view::npos) {
        return false;
    }
    usize segmentStart = 0;
    for (usize i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string_view segment = path.substr(segmentStart, i - segmentStart);
            if (segment.empty() || segment == "." || segment == "..") {
                return false;
            }
            segmentStart = i + 1;
        }
    }
    return true;
}

std::string_view urnPath(std::string_view urn)
{
    return isValidUrn(urn) ? urn.substr(AssetScheme.size()) : std::string_view{};
}

void ContentMounts::mountDirectory(std::filesystem::path root)
{
    Mount mount;
    mount.directory = std::move(root);
    m_mounts.push_back(std::move(mount));
}

std::optional<core::EngineError> ContentMounts::mountPack(const std::filesystem::path& pack,
                                                          const std::filesystem::path& manifest)
{
    std::filesystem::path manifestPath = manifest;
    if (manifestPath.empty()) {
        manifestPath = pack;
        manifestPath.replace_extension();
        manifestPath += ".manifest.json";
    }

    Mount mount;
    mount.pack = std::make_unique<Pack>();
    if (auto error = openPackFile(pack, *mount.pack)) {
        return error;
    }

    std::string text;
    if (!platform::readTextFile(manifestPath, text)) {
        const I18nArg args[] = {{"content", manifestPath.string()}};
        return core::makeError(LUAUG_TR("asset.manifest.err.open_failed"), args);
    }

    core::JsonDocument document;
    const core::JsonDocument::ParseResult parsed = document.parse(text, manifestPath.string());
    if (!parsed.ok) {
        const I18nArg args[] = {{"detail", parsed.diagnostic}};
        return core::makeError(LUAUG_TR("asset.manifest.err.malformed"), args);
    }

    const core::JsonValue root = document.root();
    if (root["format"].asString() != "luaug-content-manifest") {
        const I18nArg args[] = {{"detail", "not a LuauG content manifest"}};
        return core::makeError(LUAUG_TR("asset.manifest.err.malformed"), args);
    }

    const core::JsonValue assets = root["assets"];
    for (usize i = 0; i < assets.size(); ++i) {
        const core::JsonValue entry = assets.at(i);
        const std::string_view urn = entry["urn"].asString();
        const std::string_view hex = entry["hash"].asString();

        core::ContentHash hash;
        if (!isValidUrn(urn) || !core::parseHex(hex, hash)) {
            const I18nArg args[] = {{"detail", std::string(urn)}};
            return core::makeError(LUAUG_TR("asset.manifest.err.malformed"), args);
        }

        const PackEntry* const found = mount.pack->find(hash);
        if (found == nullptr) {
            // Refused at mount rather than at first use. A game that starts and
            // then cannot find its world is worse than one that says why it
            // will not start.
            const I18nArg args[] = {{"content", std::string(urn)}};
            return core::makeError(LUAUG_TR("asset.manifest.err.missing_blob"), args);
        }

        PackEntry record = *found;
        record.kind = kindFromName(entry["kind"].asString());
        mount.byUrn.emplace(std::string(urn), record);
    }

    m_mounts.push_back(std::move(mount));
    return std::nullopt;
}

void ContentMounts::clear()
{
    m_mounts.clear();
}

ResolvedContent ContentMounts::resolve(std::string_view urn) const
{
    ResolvedContent result;
    if (!isValidUrn(urn)) {
        return result;
    }
    const std::string_view relative = urn.substr(AssetScheme.size());

    // Reverse order: a later mount wins, so a project overrides engine content
    // by mounting after it.
    for (auto mount = m_mounts.rbegin(); mount != m_mounts.rend(); ++mount) {
        if (mount->pack != nullptr) {
            const auto entry = mount->byUrn.find(std::string(urn));
            if (entry != mount->byUrn.end()) {
                result.source = ResolvedContent::Source::Pack;
                result.kind = entry->second.kind;
                result.hash = entry->second.hash;
                result.bytes = mount->pack->blob(entry->second.hash);
                return result;
            }
            continue;
        }

        std::filesystem::path candidate = mount->directory;
        candidate /= std::filesystem::path(relative);
        // `platform::fileExists` -- an open and a close -- rather than
        // `std::filesystem::exists`, because inside an APK the content
        // directory is a set of zip entries and no path any C runtime can stat
        // (file.h). Existence is "can it be opened".
        //
        // It used to be `platform::readFile`, which READ THE WHOLE FILE and
        // threw the bytes away (D039). Every streamed chunk was therefore read
        // twice -- once synchronously here, on the frame thread, inside the
        // streaming pump, and once asynchronously by the caller that had just
        // asked where it was. On a slow filesystem that first read was a hitch
        // of tens of milliseconds.
        if (platform::fileExists(candidate)) {
            result.source = ResolvedContent::Source::Loose;
            result.path = std::move(candidate);
            return result;
        }
    }
    return result;
}

std::span<const std::byte> ContentMounts::blob(const core::ContentHash& hash) const
{
    for (auto mount = m_mounts.rbegin(); mount != m_mounts.rend(); ++mount) {
        if (mount->pack == nullptr) {
            continue;
        }
        const std::span<const std::byte> bytes = mount->pack->blob(hash);
        if (!bytes.empty() || mount->pack->contains(hash)) {
            return bytes;
        }
    }
    return {};
}

std::vector<std::string> ContentMounts::packedUrns() const
{
    std::vector<std::string> urns;
    for (const Mount& mount : m_mounts) {
        for (const auto& entry : mount.byUrn) {
            urns.push_back(entry.first);
        }
    }
    // Sorted, and deduplicated: the same URN in two mounts is one name, and a
    // caller listing content should see what `resolve` would answer rather than
    // the mount history.
    std::sort(urns.begin(), urns.end());
    urns.erase(std::unique(urns.begin(), urns.end()), urns.end());
    return urns;
}

} // namespace luaug::asset
