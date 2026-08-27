#include "luaug/replication/field.h"

#include <cstddef>
#include <cstring>

#include "wire_schema.gen.h"

namespace luaug::replication {

// **The wire layout is the C++ layout, and these hold that true.**
//
// `encodeField` copies a cell's first `wireBytes` verbatim rather than
// switching on the encoding and writing member by member, which is right only
// while every type it names is tightly packed and starts at offset zero. A
// padding byte appearing in one of them would put garbage on the wire and read
// it back as a number, silently, on one compiler and not another -- so it is a
// build failure here instead.
static_assert(sizeof(core::Vec3) == 12, "Vec3 must be three tightly packed f32");
static_assert(sizeof(core::DVec3) == 24, "DVec3 must be three tightly packed f64");
static_assert(sizeof(core::Mat3) == 36, "Mat3 must be nine tightly packed f32");
static_assert(offsetof(core::CFrameD, position) == 0, "a CFrameD's position must lead");
static_assert(offsetof(core::CFrameD, rotation) == 24, "a CFrameD's rotation must follow its position");

namespace {

using core::u8;
using core::usize;

// **Clear, then write.** A cell is compared as bytes, so a `Vec3` written over a
// `CFrameD` would leave the rest of the old value behind and two equal Vec3s
// would compare unequal -- a field that never changed, sent every tick, for
// ever. It is one `memset` and it is the reason this file has a helper rather
// than eleven `std::memcpy` calls.
template <class T>
void store(FieldValue& out, const T& value) noexcept
{
    static_assert(sizeof(T) <= FieldValue::Bytes);
    out.raw.fill(0);
    std::memcpy(out.raw.data(), &value, sizeof(T));
}

template <class T>
[[nodiscard]] T load(const FieldValue& value) noexcept
{
    static_assert(sizeof(T) <= FieldValue::Bytes);
    T result{};
    std::memcpy(&result, value.raw.data(), sizeof(T));
    return result;
}

void putBytes(std::vector<u8>& out, const void* source, usize count)
{
    const auto* bytes = static_cast<const u8*>(source);
    out.insert(out.end(), bytes, bytes + count);
}

} // namespace

void setBool(FieldValue& out, bool value) noexcept
{
    const u8 byte = value ? 1u : 0u;
    store(out, byte);
}

void setU8(FieldValue& out, core::u8 value) noexcept
{
    store(out, value);
}

void setU16(FieldValue& out, core::u16 value) noexcept
{
    store(out, value);
}

void setU32(FieldValue& out, core::u32 value) noexcept
{
    store(out, value);
}

void setI32(FieldValue& out, core::i32 value) noexcept
{
    store(out, value);
}

void setF32(FieldValue& out, float value) noexcept
{
    store(out, value);
}

void setF64(FieldValue& out, double value) noexcept
{
    store(out, value);
}

void setVec3(FieldValue& out, core::Vec3 value) noexcept
{
    store(out, value);
}

void setPosition(FieldValue& out, core::DVec3 value) noexcept
{
    store(out, value);
}

void setCFrame(FieldValue& out, const core::CFrameD& value) noexcept
{
    // **Written member by member rather than as one struct**, and this is the
    // one type that needs it.
    //
    // `sizeof(CFrameD)` is 64: twenty-four bytes of position, thirty-six of
    // rotation, and four of padding the compiler inserts to align the whole to
    // eight. A `memcpy` of the struct copies that padding out of an
    // uninitialised source, so two CFrames with identical values land in cells
    // that differ in four bytes -- and a cell is compared as bytes, which is
    // what makes the diff cheap. The field would be sent every tick, for the
    // life of the connection, with nothing anywhere reporting a fault.
    //
    // Caught by a round-trip test: the wire carries sixty bytes, the decoder
    // zeroes the rest, and the result did not equal what went in.
    out.raw.fill(0);
    std::memcpy(out.raw.data(), &value.position, sizeof(value.position));
    std::memcpy(out.raw.data() + sizeof(value.position), &value.rotation, sizeof(value.rotation));
}

void setNetId(FieldValue& out, NetId value) noexcept
{
    store(out, value.value);
}

bool asBool(const FieldValue& value) noexcept
{
    return load<u8>(value) != 0;
}

core::u32 asU32(const FieldValue& value) noexcept
{
    return load<core::u32>(value);
}

float asF32(const FieldValue& value) noexcept
{
    return load<float>(value);
}

core::Vec3 asVec3(const FieldValue& value) noexcept
{
    return load<core::Vec3>(value);
}

core::CFrameD asCFrame(const FieldValue& value) noexcept
{
    // The mirror of `setCFrame`: read the two members, never the struct, so a
    // cell whose padding bytes are zero produces the same object either way.
    core::CFrameD result;
    std::memcpy(&result.position, value.raw.data(), sizeof(result.position));
    std::memcpy(&result.rotation, value.raw.data() + sizeof(result.position), sizeof(result.rotation));
    return result;
}

NetId asNetId(const FieldValue& value) noexcept
{
    return NetId{load<core::u32>(value)};
}

usize wireBytes(generated::Encoding encoding) noexcept
{
    switch (encoding) {
    case generated::Encoding::Bool:
    case generated::Encoding::U8:
        return 1;
    case generated::Encoding::U16:
        return 2;
    case generated::Encoding::U32:
    case generated::Encoding::I32:
    case generated::Encoding::F32:
    case generated::Encoding::NameAtom:
    case generated::Encoding::InstanceRef:
        return 4;
    case generated::Encoding::F64:
        return 8;
    case generated::Encoding::Vector3:
    case generated::Encoding::Color3:
        return 12;
    case generated::Encoding::Position:
        return 24;
    case generated::Encoding::CFrameD:
        // Three f64 of position and nine f32 of rotation.
        //
        // **The rotation is not quantised, and that is this protocol version's
        // decision rather than an oversight.** A quaternion at sixteen bits an
        // axis would cost twenty of these sixty bytes -- a real saving -- and it
        // would also introduce an error budget nobody has measured against a
        // physics mirror that reads the result back. Version 2 can do it with a
        // new encoding beside this one; shipping an unmeasured quantiser is the
        // worse order, and it is the same argument `.lterrain` made about its
        // compression byte.
        return 24 + 36;
    }
    return 0;
}

void encodeField(std::vector<u8>& out, generated::Encoding encoding, const FieldValue& value)
{
    // **The cell's first `wireBytes` are the value**, because every setter
    // stores at offset zero and every encoding is its own C++ type laid out
    // little-endian. This is one `insert` rather than a switch with eleven arms
    // that would each say the same thing.
    const usize count = wireBytes(encoding);
    if (count == 0 || count > FieldValue::Bytes) {
        return;
    }
    putBytes(out, value.raw.data(), count);
}

bool decodeField(std::span<const u8> bytes, usize& at, generated::Encoding encoding, FieldValue& out) noexcept
{
    const usize count = wireBytes(encoding);
    if (count == 0 || count > FieldValue::Bytes || at + count > bytes.size()) {
        return false;
    }
    // Cleared first, for the reason `store` is: a decoded cell is compared
    // against a baseline cell, and leftover bytes would make two equal values
    // differ.
    out.raw.fill(0);
    std::memcpy(out.raw.data(), bytes.data() + at, count);
    at += count;
    return true;
}

} // namespace luaug::replication
