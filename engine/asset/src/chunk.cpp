#include "luaug/asset/chunk.h"

#include "luaug/core/i18n.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/text_key.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace luaug::asset {
namespace {

using core::I18nArg;
using core::u64;

// magic(4) + version(4) + flags(4) + id(12) + bounds(48) + instanceCount(4) +
// stringCount(4). Little-endian scalars, the same convention `pack.cpp` states.
constexpr usize HeaderBytes = 80;
constexpr usize InstanceRecordBytes = 96;

void writeU32(std::vector<std::byte>& out, u32 value)
{
    for (usize i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
}

void writeF32(std::vector<std::byte>& out, f32 value)
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(out, bits);
}

void writeF64(std::vector<std::byte>& out, core::f64 value)
{
    u64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (usize i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8)) & 0xFFu));
    }
}

void writeDVec3(std::vector<std::byte>& out, core::DVec3 v)
{
    writeF64(out, v.x);
    writeF64(out, v.y);
    writeF64(out, v.z);
}

class Reader
{
public:
    Reader(std::span<const std::byte> bytes, usize at) : m_bytes(bytes), m_at(at) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] usize at() const noexcept { return m_at; }
    [[nodiscard]] bool has(usize count) const noexcept { return m_ok && m_bytes.size() - m_at >= count; }

    u32 u32v()
    {
        if (!has(4)) {
            m_ok = false;
            return 0;
        }
        u32 value = 0;
        for (usize i = 0; i < 4; ++i) {
            value |= static_cast<u32>(static_cast<unsigned char>(m_bytes[m_at + i])) << (i * 8);
        }
        m_at += 4;
        return value;
    }

    f32 f32v()
    {
        const u32 bits = u32v();
        f32 value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    core::f64 f64v()
    {
        if (!has(8)) {
            m_ok = false;
            return 0.0;
        }
        u64 bits = 0;
        for (usize i = 0; i < 8; ++i) {
            bits |= static_cast<u64>(static_cast<unsigned char>(m_bytes[m_at + i])) << (i * 8);
        }
        m_at += 8;
        core::f64 value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    core::DVec3 dvec3()
    {
        core::DVec3 value;
        value.x = f64v();
        value.y = f64v();
        value.z = f64v();
        return value;
    }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
    bool m_ok = true;
};

[[nodiscard]] core::EngineError malformed()
{
    return core::makeError(LUAUG_TR("asset.chunk.err.malformed"));
}

} // namespace

const ChunkIndexEntry* ChunkIndex::find(ChunkId id) const noexcept
{
    const auto at = std::lower_bound(chunks.begin(), chunks.end(), id,
                                     [](const ChunkIndexEntry& entry, ChunkId key) { return entry.id < key; });
    if (at == chunks.end() || !(at->id == id)) {
        return nullptr;
    }
    return &*at;
}

ChunkId chunkIdAt(core::DVec3 position, f32 chunkSize, i32 layer) noexcept
{
    if (chunkSize <= 0.0f) {
        return ChunkId{0, 0, layer};
    }
    const auto size = static_cast<core::f64>(chunkSize);
    // `floor` rather than a cast, because a cast truncates toward zero and
    // would put x = -0.5 and x = +0.5 in the same cell -- a seam down the
    // middle of the world that only shows up on the negative side of it.
    return ChunkId{static_cast<i32>(std::floor(position.x / size)), static_cast<i32>(std::floor(position.z / size)),
                   layer};
}

core::DAABB chunkBounds(ChunkId id, f32 chunkSize) noexcept
{
    const auto size = static_cast<core::f64>(chunkSize);
    const core::f64 minX = static_cast<core::f64>(id.x) * size;
    const core::f64 minZ = static_cast<core::f64>(id.z) * size;
    // Vertically unbounded on purpose: the grid is 2D (architecture.md §10) and
    // a chunk holds everything above and below its footprint. A y-extent here
    // would be a third axis the index does not have.
    return core::DAABB::fromMinMax(core::DVec3{minX, -core::kInfinityD, minZ},
                                   core::DVec3{minX + size, core::kInfinityD, minZ + size});
}

std::vector<std::byte> encodeChunk(const Chunk& chunk)
{
    std::vector<std::byte> out;
    out.insert(out.end(), reinterpret_cast<const std::byte*>(ChunkMagic),
               reinterpret_cast<const std::byte*>(ChunkMagic) + 4);
    writeU32(out, ChunkFormatVersion);
    writeU32(out, 0);
    writeU32(out, static_cast<u32>(chunk.id.x));
    writeU32(out, static_cast<u32>(chunk.id.z));
    writeU32(out, static_cast<u32>(chunk.id.layer));
    writeDVec3(out, chunk.bounds.min);
    writeDVec3(out, chunk.bounds.max);
    writeU32(out, static_cast<u32>(chunk.instances.size()));
    writeU32(out, static_cast<u32>(chunk.strings.size()));

    for (const ChunkInstance& instance : chunk.instances) {
        writeU32(out, static_cast<u32>(instance.kind));
        writeU32(out, instance.shape);
        writeU32(out, instance.anchored ? 1u : 0u);
        writeU32(out, instance.name);
        writeU32(out, instance.meshContent);
        writeF32(out, instance.transparency);
        writeDVec3(out, instance.cframe.position);
        for (usize row = 0; row < 3; ++row) {
            for (usize column = 0; column < 3; ++column) {
                writeF32(out, instance.cframe.rotation.m[row][column]);
            }
        }
        writeF32(out, instance.size.x);
        writeF32(out, instance.size.y);
        writeF32(out, instance.size.z);
        writeF32(out, instance.color.r);
        writeF32(out, instance.color.g);
        writeF32(out, instance.color.b);
    }

    for (const std::string& text : chunk.strings) {
        writeU32(out, static_cast<u32>(text.size()));
        out.insert(out.end(), reinterpret_cast<const std::byte*>(text.data()),
                   reinterpret_cast<const std::byte*>(text.data()) + text.size());
    }
    return out;
}

std::optional<core::EngineError> decodeChunk(std::span<const std::byte> bytes, Chunk& out)
{
    out = Chunk{};

    if (bytes.size() < HeaderBytes) {
        return malformed();
    }
    if (std::memcmp(bytes.data(), ChunkMagic, 4) != 0) {
        return core::makeError(LUAUG_TR("asset.chunk.err.magic"));
    }

    Reader reader(bytes, 4);
    const u32 version = reader.u32v();
    if (version != ChunkFormatVersion) {
        const I18nArg args[] = {{"found", std::to_string(version)}, {"expected", std::to_string(ChunkFormatVersion)}};
        return core::makeError(LUAUG_TR("asset.chunk.err.version"), args);
    }
    if (reader.u32v() != 0) {
        return malformed();
    }

    out.id.x = static_cast<i32>(reader.u32v());
    out.id.z = static_cast<i32>(reader.u32v());
    out.id.layer = static_cast<i32>(reader.u32v());
    out.bounds.min = reader.dvec3();
    out.bounds.max = reader.dvec3();

    const u32 instanceCount = reader.u32v();
    const u32 stringCount = reader.u32v();
    if (!reader.ok()) {
        return malformed();
    }

    // Counted before it is believed, which is M7's own finding from the mesh
    // format: a bounds-checked reader is not a safe reader, because the
    // allocation happens first.
    if (instanceCount > MaxChunkInstances) {
        return core::makeError(LUAUG_TR("asset.chunk.err.too_large"));
    }
    const usize remaining = bytes.size() - reader.at();
    if (static_cast<u64>(instanceCount) * InstanceRecordBytes > remaining) {
        return malformed();
    }

    out.instances.reserve(instanceCount);
    for (u32 i = 0; i < instanceCount; ++i) {
        ChunkInstance instance;
        const u32 kind = reader.u32v();
        if (kind > static_cast<u32>(ChunkInstance::Kind::MeshPart)) {
            return malformed();
        }
        instance.kind = static_cast<ChunkInstance::Kind>(kind);
        instance.shape = static_cast<u8>(reader.u32v());
        instance.anchored = reader.u32v() != 0;
        instance.name = reader.u32v();
        instance.meshContent = reader.u32v();
        instance.transparency = reader.f32v();
        instance.cframe.position = reader.dvec3();
        for (usize row = 0; row < 3; ++row) {
            for (usize column = 0; column < 3; ++column) {
                instance.cframe.rotation.m[row][column] = reader.f32v();
            }
        }
        instance.size = core::Vec3{reader.f32v(), reader.f32v(), reader.f32v()};
        instance.color = core::Color3{reader.f32v(), reader.f32v(), reader.f32v()};
        if (!reader.ok()) {
            return malformed();
        }
        out.instances.push_back(instance);
    }

    out.strings.reserve(std::min<u32>(stringCount, 4096));
    for (u32 i = 0; i < stringCount; ++i) {
        const u32 length = reader.u32v();
        if (!reader.ok() || !reader.has(length)) {
            return malformed();
        }
        out.strings.emplace_back(reinterpret_cast<const char*>(bytes.data()) + reader.at(), length);
        reader = Reader(bytes, reader.at() + length);
    }

    // A string index that names nothing is a mesh URN nobody can resolve, and
    // it would surface as an invisible part rather than as an error.
    for (const ChunkInstance& instance : out.instances) {
        if (instance.name != ChunkInstance::NoString && instance.name >= out.strings.size()) {
            return malformed();
        }
        if (instance.meshContent != ChunkInstance::NoString && instance.meshContent >= out.strings.size()) {
            return malformed();
        }
    }
    return std::nullopt;
}

std::string writeChunkIndex(const ChunkIndex& index)
{
    core::JsonWriter json;
    json.beginObject();
    json.field("format", "luaug-chunk-index");
    json.field("version", static_cast<u64>(ChunkFormatVersion));
    json.field("chunkSize", static_cast<core::f64>(index.chunkSize));

    json.key("chunks");
    json.beginArray();
    for (const ChunkIndexEntry& entry : index.chunks) {
        json.beginObject();
        json.field("x", static_cast<core::i64>(entry.id.x));
        json.field("z", static_cast<core::i64>(entry.id.z));
        json.field("layer", static_cast<core::i64>(entry.id.layer));
        json.field("urn", entry.urn);
        json.field("instances", static_cast<u64>(entry.instanceCount));
        json.field("bytes", static_cast<u64>(entry.bytes));
        json.field("minY", entry.bounds.min.y);
        json.field("maxY", entry.bounds.max.y);
        json.endObject();
    }
    json.endArray();
    json.endObject();

    std::string text = json.text();
    text.push_back('\n');
    return text;
}

std::optional<core::EngineError> readChunkIndex(std::string_view json, ChunkIndex& out)
{
    out = ChunkIndex{};

    core::JsonDocument document;
    const core::JsonDocument::ParseResult parsed = document.parse(json, "chunk index");
    if (!parsed.ok) {
        const I18nArg args[] = {{"detail", parsed.diagnostic}};
        return core::makeError(LUAUG_TR("asset.chunk.err.index_malformed"), args);
    }

    const core::JsonValue root = document.root();
    if (root["format"].asString() != "luaug-chunk-index") {
        const I18nArg args[] = {{"detail", "not a LuauG chunk index"}};
        return core::makeError(LUAUG_TR("asset.chunk.err.index_malformed"), args);
    }

    out.chunkSize = static_cast<f32>(root["chunkSize"].asNumber(static_cast<core::f64>(DefaultChunkSize)));
    if (!(out.chunkSize > 0.0f)) {
        const I18nArg args[] = {{"detail", "a chunk size of zero"}};
        return core::makeError(LUAUG_TR("asset.chunk.err.index_malformed"), args);
    }

    const core::JsonValue chunks = root["chunks"];
    out.chunks.reserve(chunks.size());
    for (usize i = 0; i < chunks.size(); ++i) {
        const core::JsonValue row = chunks.at(i);
        ChunkIndexEntry entry;
        entry.id.x = static_cast<i32>(row["x"].asInteger());
        entry.id.z = static_cast<i32>(row["z"].asInteger());
        entry.id.layer = static_cast<i32>(row["layer"].asInteger());
        entry.urn = std::string(row["urn"].asString());
        entry.instanceCount = static_cast<u32>(row["instances"].asInteger());
        entry.bytes = static_cast<u32>(row["bytes"].asInteger());

        entry.bounds = chunkBounds(entry.id, out.chunkSize);
        // The vertical extent is the one thing the footprint cannot derive, and
        // it is what lets a flat world's chunks be scored without pretending
        // they are infinitely tall.
        entry.bounds.min.y = row["minY"].asNumber(-core::kInfinityD);
        entry.bounds.max.y = row["maxY"].asNumber(core::kInfinityD);

        if (entry.urn.empty()) {
            const I18nArg args[] = {{"detail", "a chunk with no URN"}};
            return core::makeError(LUAUG_TR("asset.chunk.err.index_malformed"), args);
        }
        out.chunks.push_back(std::move(entry));
    }

    std::sort(out.chunks.begin(), out.chunks.end(),
              [](const ChunkIndexEntry& a, const ChunkIndexEntry& b) { return a.id < b.id; });
    return std::nullopt;
}

} // namespace luaug::asset
