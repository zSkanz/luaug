#include "luaug/app/partition_cache.h"

#include "luaug/core/content_hash.h"
#include "luaug/core/i18n.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/scene/world.h"

#include <map>
#include <system_error>
#include <vector>

namespace luaug::app {
namespace {

using core::LogLevel;

// Bumped whenever the partitioner's own rules change, so a cache written by an
// older build is not believed. It is part of the key rather than a field to
// check, which means an upgrade leaves the old directory to be pruned rather
// than to be repaired.
constexpr core::u32 PartitionRules = 1;

constexpr std::string_view kManifest = "partition.json";
constexpr std::string_view kScene = "scene.json";
constexpr std::string_view kIndex = "index.json";

[[nodiscard]] std::string cellName(asset::ChunkId id)
{
    return "cell_" + std::to_string(id.x) + "_" + std::to_string(id.z) + "_" + std::to_string(id.layer) + ".lchunk";
}

// What a cache directory records about the stamps its partition read, so that
// editing one repartitions. Nothing in the scene's own bytes would say a stamp
// moved, and a partition that believed them would put yesterday's buildings in
// today's world.
struct StampHash
{
    std::string path;
    std::string hash;
};

[[nodiscard]] std::string writeManifest(const std::vector<StampHash>& stamps)
{
    core::JsonWriter json;
    json.beginObject();
    json.field("format", "luaug-partition");
    json.field("rules", static_cast<core::u64>(PartitionRules));
    json.key("stamps");
    json.beginArray();
    for (const StampHash& stamp : stamps) {
        json.beginObject();
        json.field("path", stamp.path);
        json.field("hash", stamp.hash);
        json.endObject();
    }
    json.endArray();
    json.endObject();
    std::string text = json.text();
    text.push_back('\n');
    return text;
}

// True when every stamp the manifest names still hashes to what it did.
[[nodiscard]] bool manifestHolds(const std::filesystem::path& manifestPath, const std::filesystem::path& contentRoot)
{
    std::string text;
    if (!platform::readTextFile(manifestPath, text)) {
        return false;
    }

    core::JsonDocument document;
    if (!document.parse(text, "partition manifest").ok) {
        return false;
    }
    const core::JsonValue root = document.root();
    if (root["format"].asString() != "luaug-partition" ||
        root["rules"].asInteger() != static_cast<core::i64>(PartitionRules)) {
        return false;
    }

    const core::JsonValue stamps = root["stamps"];
    for (core::usize i = 0; i < stamps.size(); ++i) {
        const core::JsonValue row = stamps.at(i);
        const std::string_view relative = row["path"].asString();
        std::string stampText;
        if (!platform::readTextFile(contentRoot / std::filesystem::path(relative), stampText)) {
            // The stamp is gone. The partition that read it described a world
            // that no longer exists, so it is redone rather than trusted.
            return false;
        }
        if (core::hashText(stampText).toHex() != row["hash"].asString()) {
            return false;
        }
    }
    return true;
}

// Everything under `.luaug/partition` that is not this scene's. A directory per
// scene version otherwise accumulates one per edit, and the cache would grow
// without bound in the one place a person never looks.
void pruneSiblings(const std::filesystem::path& root, const std::filesystem::path& keep)
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error)) {
        if (entry.path() == keep) {
            continue;
        }
        std::error_code removeError;
        std::filesystem::remove_all(entry.path(), removeError);
    }
}

} // namespace

PartitionOutcome partitionProject(scene::World& registries, const std::filesystem::path& projectRoot,
                                  const std::filesystem::path& contentRoot, const std::filesystem::path& scenePath,
                                  const asset::ChunkIndex* built)
{
    PartitionOutcome outcome;
    if (projectRoot.empty() || scenePath.empty()) {
        return outcome;
    }

    std::string sceneText;
    if (!platform::readTextFile(scenePath, sceneText)) {
        return outcome;
    }

    const std::filesystem::path root = projectRoot / ".luaug" / "partition";
    const std::filesystem::path directory = root / core::hashText(sceneText).toHex();
    outcome.directory = directory;

    // **The cache first, because that is the whole point of it.** A person
    // pressing play twice pays for the partition once, and a shipping build
    // that warmed this directory pays nothing at all.
    const std::filesystem::path indexPath = directory / std::filesystem::path(kIndex);
    if (manifestHolds(directory / std::filesystem::path(kManifest), contentRoot)) {
        std::string indexText;
        if (platform::readTextFile(indexPath, indexText)) {
            if (!asset::readChunkIndex(indexText, outcome.index).has_value()) {
                outcome.active = !outcome.index.chunks.empty();
                // A partition that produced no cells leaves the ORIGINAL scene
                // to boot. The residual is byte-identical to it in that case,
                // and pointing at a copy of a file would be a dependency on the
                // cache for a project that has no use for one.
                if (outcome.active) {
                    outcome.scenePath = directory / std::filesystem::path(kScene);
                }
                return outcome;
            }
        }
    }

    // Recorded as the partition reads them, so the manifest names exactly the
    // stamps this world depends on rather than every stamp the project holds.
    std::map<std::string, std::string> stampHashes;
    const scene::StampSource stamps = [&](std::string_view stamp) -> std::optional<std::string> {
        std::string text;
        if (!platform::readTextFile(contentRoot / std::filesystem::path(stamp), text)) {
            return std::nullopt;
        }
        stampHashes.emplace(std::string(stamp), core::hashText(text).toHex());
        return text;
    };

    scene::PartitionSettings settings;
    settings.chunkSize = built != nullptr && !built->chunks.empty() ? built->chunkSize : asset::DefaultChunkSize;
    if (built != nullptr) {
        settings.cellTaken = [built](asset::ChunkId id) { return built->find(id) != nullptr; };
    }

    // The directory before the partition, because the sink writes into it as
    // each cell is finished. A cache that cannot be written is a partition that
    // runs every time -- slower and still correct -- and it is named rather
    // than silent: on a read-only install that is the difference between "this
    // is slow" and "this is broken".
    if (!platform::createDirectories(directory)) {
        const core::I18nArg args[] = {{"path", directory.string()}};
        core::log(LogLevel::Warn, LUAUG_TR("app.warn.partition_uncached"), args);
        return outcome;
    }

    // Written as it is finished rather than collected: a world's cells are
    // never all resident at once on the way OUT either, which is the same
    // property the partitioner keeps on the way in.
    bool wroteEverything = true;
    const scene::PartitionSink sink = [&](const asset::Chunk& cell) {
        scene::PartitionCellWritten written;
        const std::vector<std::byte> bytes = asset::encodeChunk(cell);
        const std::string name = cellName(cell.id);
        if (!platform::writeFile(directory / std::filesystem::path(name), bytes)) {
            wroteEverything = false;
            return written;
        }
        written.bytes = static_cast<core::u32>(bytes.size());
        written.urn = name;
        return written;
    };

    scene::PartitionResult result;
    if (const std::optional<core::EngineError> error =
            scene::partitionScene(registries, sceneText, settings, stamps, sink, result);
        error.has_value()) {
        core::logText(LogLevel::Warn, error->message);
        return outcome;
    }

    outcome.report = result.report;
    outcome.repartitioned = true;
    outcome.index = std::move(result.index);

    std::vector<StampHash> recorded;
    recorded.reserve(stampHashes.size());
    for (const auto& entry : stampHashes) {
        recorded.push_back(StampHash{entry.first, entry.second});
    }

    // The manifest LAST, because it is what says the cache is usable: a run
    // interrupted between the cells and the manifest leaves a directory the
    // next run rebuilds rather than half-believes.
    const bool wrote = wroteEverything &&
                       platform::writeTextFile(directory / std::filesystem::path(kScene), result.scene) &&
                       platform::writeTextFile(indexPath, asset::writeChunkIndex(outcome.index)) &&
                       platform::writeTextFile(directory / std::filesystem::path(kManifest), writeManifest(recorded));
    if (!wrote) {
        const core::I18nArg args[] = {{"path", directory.string()}};
        core::log(LogLevel::Warn, LUAUG_TR("app.warn.partition_uncached"), args);
        outcome.active = false;
        return outcome;
    }

    pruneSiblings(root, directory);

    outcome.active = !outcome.index.chunks.empty();
    if (outcome.active) {
        outcome.scenePath = directory / std::filesystem::path(kScene);
        const core::I18nArg args[] = {{"cells", static_cast<core::i64>(outcome.report.cells)},
                                      {"count", static_cast<core::i64>(outcome.report.records)},
                                      {"kept", static_cast<core::i64>(outcome.report.kept)}};
        core::log(LogLevel::Info, LUAUG_TR("app.info.partitioned"), args);
    }
    return outcome;
}

} // namespace luaug::app
