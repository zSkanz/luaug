#include "luaug/asset/terrain.h"

#include <cstring>

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::u8;
using core::usize;

// `LTRN`, little-endian, and a version beside it. A format with no version is a
// format that can only ever be read by the code that wrote it.
constexpr u32 Magic = 0x4E52544Cu;
constexpr u32 Version = 1;

void putU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

void putI32(std::vector<u8>& out, i32 value)
{
    // Through the unsigned form, so the encoding is two's complement by
    // construction rather than by whatever the platform does with a shift on a
    // negative.
    u32 wide = 0;
    std::memcpy(&wide, &value, sizeof(wide));
    putU32(out, wide);
}

void putF32(std::vector<u8>& out, float value)
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(out, bits);
}

struct Reader
{
    std::span<const u8> bytes;
    usize at = 0;
    bool ok = true;

    [[nodiscard]] u32 u32Value()
    {
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0;
        }
        const u32 value = static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8) |
                          (static_cast<u32>(bytes[at + 2]) << 16) | (static_cast<u32>(bytes[at + 3]) << 24);
        at += 4;
        return value;
    }

    [[nodiscard]] i32 i32Value()
    {
        const u32 wide = u32Value();
        i32 value = 0;
        std::memcpy(&value, &wide, sizeof(value));
        return value;
    }

    [[nodiscard]] float f32Value()
    {
        const u32 bits = u32Value();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};

// PackBits, which is the smallest run-length scheme that does not make
// incompressible data twice its size.
//
// **Chosen over a real compressor on purpose.** Zstd is vendored inside
// basis_universal, and reaching into another library's bundled dependency to
// compress a scene file is a dependency decision that deserves an ADR rather
// than a convenience. What this data actually looks like is long runs: a brick
// is mostly saturated distance, a tile's materials are usually one value, and
// flat ground repeats the same four height bytes for a thousand columns. A run
// coder captures nearly all of that, and the worst case costs one byte per 128.
//
// A control byte n < 128 means "the next n + 1 bytes are literal"; n >= 128
// means "the next byte repeats 257 - n times" (2 to 129).
void packBits(const std::vector<u8>& plain, std::vector<u8>& out)
{
    usize at = 0;
    while (at < plain.size()) {
        // How long the run starting here is.
        usize run = 1;
        while (at + run < plain.size() && plain[at + run] == plain[at] && run < 129) {
            ++run;
        }

        if (run >= 2) {
            out.push_back(static_cast<u8>(257 - run));
            out.push_back(plain[at]);
            at += run;
            continue;
        }

        // A literal stretch, which ends where a run of three or more begins --
        // two is not worth breaking a literal for, because the break costs a
        // control byte of its own.
        usize literal = 1;
        while (at + literal < plain.size() && literal < 128) {
            const bool runAhead = at + literal + 2 < plain.size() && plain[at + literal] == plain[at + literal + 1] &&
                                  plain[at + literal] == plain[at + literal + 2];
            if (runAhead) {
                break;
            }
            ++literal;
        }
        out.push_back(static_cast<u8>(literal - 1));
        out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(at),
                   plain.begin() + static_cast<std::ptrdiff_t>(at + literal));
        at += literal;
    }
}

[[nodiscard]] bool unpackBits(std::span<const u8> packed, usize plainSize, std::vector<u8>& out)
{
    out.clear();
    out.reserve(plainSize);
    usize at = 0;
    while (at < packed.size() && out.size() < plainSize) {
        const u8 control = packed[at++];
        if (control < 128) {
            const usize literal = static_cast<usize>(control) + 1;
            if (at + literal > packed.size() || out.size() + literal > plainSize) {
                return false;
            }
            out.insert(out.end(), packed.begin() + static_cast<std::ptrdiff_t>(at),
                       packed.begin() + static_cast<std::ptrdiff_t>(at + literal));
            at += literal;
            continue;
        }
        const usize run = 257 - static_cast<usize>(control);
        if (at >= packed.size() || out.size() + run > plainSize) {
            return false;
        }
        out.insert(out.end(), run, packed[at]);
        ++at;
    }
    return out.size() == plainSize;
}

} // namespace

std::vector<u8> encodeTerrain(const TerrainField& field)
{
    // The body first, then the whole of it through the run coder. One coder over
    // one stream rather than three over three arrays: the runs that matter cross
    // array boundaries anyway (a tile of flat ground is a run of heights
    // followed by a run of materials), and one is one thing to get right.
    std::vector<u8> body;

    const FieldSettings& settings = field.settings();
    putF32(body, settings.voxelSize);
    putF32(body, settings.minHeight);
    putF32(body, settings.maxHeight);
    putU32(body, settings.giveUpColumns);

    const std::vector<TileKey> tiles = field.tileKeys();
    const std::vector<BrickKey> bricks = field.brickKeys();
    putU32(body, static_cast<u32>(tiles.size()));
    putU32(body, static_cast<u32>(bricks.size()));

    // **In the order the field keeps them**, which is sorted -- so the bytes are
    // a pure function of the field's contents and a scene file stays diffable
    // (R10).
    for (const TileKey key : tiles) {
        putI32(body, key.x);
        putI32(body, key.z);
        const HeightTile* tile = field.findTile(key);
        if (tile == nullptr) {
            continue;
        }
        for (const float height : tile->height) {
            putF32(body, height);
        }
        body.insert(body.end(), std::begin(tile->material), std::end(tile->material));
    }

    for (const BrickKey key : bricks) {
        putI32(body, key.x);
        putI32(body, key.y);
        putI32(body, key.z);
        const Brick* brick = field.findBrick(key);
        if (brick == nullptr) {
            continue;
        }
        body.insert(body.end(), std::begin(brick->sd), std::end(brick->sd));
        body.insert(body.end(), std::begin(brick->material), std::end(brick->material));
    }

    std::vector<u8> out;
    putU32(out, Magic);
    putU32(out, Version);
    putU32(out, static_cast<u32>(body.size()));
    packBits(body, out);
    return out;
}

std::optional<TerrainField> decodeTerrain(std::span<const u8> bytes)
{
    Reader head{bytes};
    if (head.u32Value() != Magic) {
        return std::nullopt;
    }
    if (head.u32Value() != Version) {
        // **Refused rather than guessed at.** A field read by a rule it was not
        // written under is a world that loads and is silently wrong, which is
        // worse than one that says it cannot load.
        return std::nullopt;
    }
    const u32 plainSize = head.u32Value();
    if (!head.ok) {
        return std::nullopt;
    }

    std::vector<u8> body;
    if (!unpackBits(bytes.subspan(head.at), plainSize, body)) {
        return std::nullopt;
    }

    Reader in{body};
    FieldSettings settings;
    settings.voxelSize = in.f32Value();
    settings.minHeight = in.f32Value();
    settings.maxHeight = in.f32Value();
    settings.giveUpColumns = in.u32Value();
    if (!in.ok || !(settings.voxelSize > 0.0f) || !(settings.maxHeight > settings.minHeight)) {
        return std::nullopt;
    }

    const u32 tileCount = in.u32Value();
    const u32 brickCount = in.u32Value();
    if (!in.ok) {
        return std::nullopt;
    }

    TerrainField field(settings);
    std::vector<float> heights(TileArea, 0.0f);
    std::vector<u8> materials;

    for (u32 index = 0; index < tileCount; ++index) {
        const i32 x = in.i32Value();
        const i32 z = in.i32Value();
        if (!in.ok) {
            return std::nullopt;
        }
        for (usize at = 0; at < TileArea; ++at) {
            heights[at] = in.f32Value();
        }
        if (!in.ok || in.at + TileArea > body.size()) {
            return std::nullopt;
        }
        materials.assign(body.begin() + static_cast<std::ptrdiff_t>(in.at),
                         body.begin() + static_cast<std::ptrdiff_t>(in.at + TileArea));
        in.at += TileArea;
        field.setTile(TileKey{x, z}, heights, materials);
    }

    std::vector<u8> distances;
    for (u32 index = 0; index < brickCount; ++index) {
        const i32 x = in.i32Value();
        const i32 y = in.i32Value();
        const i32 z = in.i32Value();
        if (!in.ok || in.at + BrickVolume * 2 > body.size()) {
            return std::nullopt;
        }
        distances.assign(body.begin() + static_cast<std::ptrdiff_t>(in.at),
                         body.begin() + static_cast<std::ptrdiff_t>(in.at + BrickVolume));
        in.at += BrickVolume;
        materials.assign(body.begin() + static_cast<std::ptrdiff_t>(in.at),
                         body.begin() + static_cast<std::ptrdiff_t>(in.at + BrickVolume));
        in.at += BrickVolume;
        field.setBrick(BrickKey{x, y, z}, distances, materials);
    }

    return field;
}

} // namespace luaug::asset
