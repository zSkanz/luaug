#include "luaug/asset/pack.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace luaug::asset {
namespace {

using core::I18nArg;

// Scalars are little-endian, hashes are their own canonical byte order (the
// digest's, which is what `core::toBytes` writes). Stated rather than inherited
// from the host: a format whose meaning depends on the machine that wrote it is
// not a format.
void writeU32(std::vector<std::byte>& out, u32 value)
{
    for (usize i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
}

void writeU64(std::vector<std::byte>& out, u64 value)
{
    for (usize i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
}

[[nodiscard]] u32 readU32(const std::byte* bytes) noexcept
{
    u32 value = 0;
    for (usize i = 0; i < 4; ++i) {
        value |= static_cast<u32>(static_cast<unsigned char>(bytes[i])) << (i * 8);
    }
    return value;
}

[[nodiscard]] u64 readU64(const std::byte* bytes) noexcept
{
    u64 value = 0;
    for (usize i = 0; i < 8; ++i) {
        value |= static_cast<u64>(static_cast<unsigned char>(bytes[i])) << (i * 8);
    }
    return value;
}

[[nodiscard]] bool knownKind(u32 value) noexcept
{
    return value <= static_cast<u32>(AssetKind::Raw);
}

} // namespace

void PackWriter::add(const ContentHash& hash, AssetKind kind, std::span<const std::byte> bytes)
{
    if (contains(hash)) {
        return;
    }
    Blob blob;
    blob.hash = hash;
    blob.kind = kind;
    blob.bytes.assign(bytes.begin(), bytes.end());
    m_blobs.push_back(std::move(blob));
}

ContentHash PackWriter::addContent(AssetKind kind, std::span<const std::byte> bytes)
{
    const ContentHash hash = core::hashBytes(bytes);
    add(hash, kind, bytes);
    return hash;
}

bool PackWriter::contains(const ContentHash& hash) const noexcept
{
    return std::any_of(m_blobs.begin(), m_blobs.end(), [&hash](const Blob& blob) { return blob.hash == hash; });
}

u64 PackWriter::payloadBytes() const noexcept
{
    u64 total = 0;
    for (const Blob& blob : m_blobs) {
        total += blob.bytes.size();
    }
    return total;
}

std::vector<std::byte> PackWriter::build() const
{
    // Sorted by hash, which is what makes the output a function of the CONTENT
    // rather than of the order the caller happened to add things in. Two builds
    // of the same assets are byte-identical, and that is the property the CI
    // determinism check asserts.
    std::vector<const Blob*> ordered;
    ordered.reserve(m_blobs.size());
    for (const Blob& blob : m_blobs) {
        ordered.push_back(&blob);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Blob* a, const Blob* b) { return a->hash < b->hash; });

    std::vector<std::byte> out;
    out.reserve(PackHeaderBytes + static_cast<usize>(payloadBytes()) + ordered.size() * PackEntryBytes);

    out.insert(out.end(), reinterpret_cast<const std::byte*>(PackMagic),
               reinterpret_cast<const std::byte*>(PackMagic) + 4);
    writeU32(out, PackFormatVersion);
    writeU32(out, 0); // flags, reserved
    writeU32(out, static_cast<u32>(ordered.size()));
    writeU64(out, 0);                          // tocOffset, patched below
    writeU64(out, 0);                          // tocLength, patched below
    out.resize(out.size() + 16, std::byte{0}); // tocHash, patched below

    std::vector<u64> offsets;
    offsets.reserve(ordered.size());
    for (const Blob* blob : ordered) {
        offsets.push_back(out.size());
        out.insert(out.end(), blob->bytes.begin(), blob->bytes.end());
    }

    const u64 tocOffset = out.size();
    for (usize i = 0; i < ordered.size(); ++i) {
        const Blob& blob = *ordered[i];
        const std::array<std::byte, 16> hashBytes = core::toBytes(blob.hash);
        out.insert(out.end(), hashBytes.begin(), hashBytes.end());
        writeU64(out, offsets[i]);
        writeU64(out, blob.bytes.size());
        writeU64(out, blob.bytes.size());
        writeU32(out, static_cast<u32>(blob.kind));
        writeU32(out, static_cast<u32>(BlobCodec::None));
    }
    const u64 tocLength = out.size() - tocOffset;
    const ContentHash tocHash =
        core::hashBytes(std::span<const std::byte>(out.data() + tocOffset, static_cast<usize>(tocLength)));
    const std::array<std::byte, 16> tocHashBytes = core::toBytes(tocHash);

    std::byte* const header = out.data();
    for (usize i = 0; i < 8; ++i) {
        header[16 + i] = static_cast<std::byte>((tocOffset >> (i * 8)) & 0xFFu);
        header[24 + i] = static_cast<std::byte>((tocLength >> (i * 8)) & 0xFFu);
    }
    std::memcpy(header + 32, tocHashBytes.data(), tocHashBytes.size());
    return out;
}

std::optional<core::EngineError> Pack::open(std::vector<std::byte> bytes, Pack& out)
{
    return openImpl(std::move(bytes), false, out);
}

std::optional<core::EngineError> Pack::openVerified(std::vector<std::byte> bytes, Pack& out)
{
    return openImpl(std::move(bytes), true, out);
}

std::optional<core::EngineError> Pack::openImpl(std::vector<std::byte> bytes, bool verify, Pack& out)
{
    out.m_bytes.clear();
    out.m_entries.clear();

    const u64 fileSize = bytes.size();
    if (fileSize < PackHeaderBytes) {
        return core::makeError(LUAUG_TR("asset.pack.err.truncated"));
    }
    if (std::memcmp(bytes.data(), PackMagic, 4) != 0) {
        return core::makeError(LUAUG_TR("asset.pack.err.magic"));
    }

    const u32 version = readU32(bytes.data() + 4);
    if (version != PackFormatVersion) {
        const I18nArg args[] = {{"found", std::to_string(version)}, {"expected", std::to_string(PackFormatVersion)}};
        return core::makeError(LUAUG_TR("asset.pack.err.version"), args);
    }

    const u32 flags = readU32(bytes.data() + 8);
    if (flags != 0) {
        return core::makeError(LUAUG_TR("asset.pack.err.header"));
    }

    const u32 entryCount = readU32(bytes.data() + 12);
    const u64 tocOffset = readU64(bytes.data() + 16);
    const u64 tocLength = readU64(bytes.data() + 24);
    const ContentHash tocHash = core::fromBytes(std::span<const std::byte, 16>(bytes.data() + 32, 16));

    // Computed in u64 from a u32 count, so the multiply cannot wrap -- the
    // classic way a header claims a table that overlaps its own file.
    if (tocLength != static_cast<u64>(entryCount) * PackEntryBytes) {
        return core::makeError(LUAUG_TR("asset.pack.err.header"));
    }
    if (tocOffset < PackHeaderBytes || tocOffset > fileSize || tocLength > fileSize - tocOffset) {
        return core::makeError(LUAUG_TR("asset.pack.err.truncated"));
    }

    // Before a single offset is read out of it. The TOC is the one region of
    // the file with neither its own name nor a consistency relation to check it
    // against, and a flipped bit in an entry is a pack that answers a lookup
    // with somebody else's bytes.
    if (core::hashBytes(std::span<const std::byte>(bytes.data() + tocOffset, static_cast<usize>(tocLength))) !=
        tocHash) {
        return core::makeError(LUAUG_TR("asset.pack.err.toc_hash"));
    }

    std::vector<PackEntry> entries;
    entries.reserve(entryCount);
    for (u32 i = 0; i < entryCount; ++i) {
        const std::byte* const record = bytes.data() + tocOffset + static_cast<u64>(i) * PackEntryBytes;

        PackEntry entry;
        entry.hash = core::fromBytes(std::span<const std::byte, 16>(record, 16));
        entry.offset = readU64(record + 16);
        entry.storedSize = readU64(record + 24);
        entry.originalSize = readU64(record + 32);
        const u32 kind = readU32(record + 40);
        const u32 codec = readU32(record + 44);

        if (!knownKind(kind) || codec != static_cast<u32>(BlobCodec::None)) {
            return core::makeError(LUAUG_TR("asset.pack.err.entry"));
        }
        // Blobs live between the header and the TOC. Checked as a subtraction
        // against a bound rather than as `offset + size <= tocOffset`, which is
        // the same statement with an overflow in it.
        if (entry.offset < PackHeaderBytes || entry.offset > tocOffset || entry.storedSize > tocOffset - entry.offset) {
            return core::makeError(LUAUG_TR("asset.pack.err.entry"));
        }
        // v1 stores blobs uncompressed, so the two sizes must agree. A pack
        // claiming otherwise was written by something that is not this format.
        if (entry.originalSize != entry.storedSize) {
            return core::makeError(LUAUG_TR("asset.pack.err.entry"));
        }

        entry.kind = static_cast<AssetKind>(kind);
        entry.codec = BlobCodec::None;

        // Strictly ascending, which validates the sort AND rejects duplicates
        // in one comparison -- two entries under one name is a pack with two
        // answers to the same question.
        if (i > 0 && !(entries.back().hash < entry.hash)) {
            return core::makeError(LUAUG_TR("asset.pack.err.order"));
        }
        entries.push_back(entry);
    }

    if (verify) {
        for (const PackEntry& entry : entries) {
            const std::span<const std::byte> blob(bytes.data() + entry.offset, static_cast<usize>(entry.storedSize));
            if (core::hashBytes(blob) != entry.hash) {
                const I18nArg args[] = {{"hash", entry.hash.toHex()}};
                return core::makeError(LUAUG_TR("asset.pack.err.hash_mismatch"), args);
            }
        }
    }

    out.m_bytes = std::move(bytes);
    out.m_entries = std::move(entries);
    return std::nullopt;
}

const PackEntry* Pack::find(const ContentHash& hash) const noexcept
{
    const auto at = std::lower_bound(m_entries.begin(), m_entries.end(), hash,
                                     [](const PackEntry& entry, const ContentHash& key) { return entry.hash < key; });
    if (at == m_entries.end() || at->hash != hash) {
        return nullptr;
    }
    return &*at;
}

std::span<const std::byte> Pack::blob(const ContentHash& hash) const noexcept
{
    const PackEntry* const entry = find(hash);
    if (entry == nullptr) {
        return {};
    }
    return std::span<const std::byte>(m_bytes.data() + entry->offset, static_cast<usize>(entry->storedSize));
}

std::optional<core::EngineError> openPackFile(const std::filesystem::path& path, Pack& out, bool verify)
{
    std::vector<std::byte> bytes;
    if (!platform::readFile(path, bytes)) {
        const I18nArg args[] = {{"content", path.string()}};
        return core::makeError(LUAUG_TR("asset.pack.err.open_failed"), args);
    }
    return verify ? Pack::openVerified(std::move(bytes), out) : Pack::open(std::move(bytes), out);
}

} // namespace luaug::asset
