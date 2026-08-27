// One replicated field's value, and the bytes it becomes (ADR 0069).
//
// **A fixed-size cell rather than a variant**, because a baseline is one of
// these per field per instance per peer, and the thing it is compared against a
// hundred times a second is its bytes. A variant would put a discriminant beside
// a value the schema already names the type of, and a heap allocation behind
// anything that did not fit inline.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/replication/types.h"

#include <array>
#include <span>
#include <vector>

namespace luaug::replication {

namespace generated {
enum class Encoding : core::u8;
}

// The widest thing a field can hold is a `CFrameD`: three f64 of position and a
// 3x3 of rotation. Everything else fits in less and the cell is padded.
//
// **Compared as bytes and never as a type**, which is what makes `diff` one
// `memcmp` per field rather than a switch. The corollary is that every write
// into a cell has to clear it first: two `CFrameD`s that are equal must have
// equal padding, or a field that did not change would be sent every tick.
struct FieldValue
{
    static constexpr core::usize Bytes = 64;

    std::array<core::u8, Bytes> raw{};

    [[nodiscard]] bool operator==(const FieldValue& other) const noexcept { return raw == other.raw; }
};

static_assert(sizeof(core::DVec3) + sizeof(core::Mat3) <= FieldValue::Bytes,
              "a CFrameD must fit in a field cell, or the widest encoding has outgrown it");

// **Every setter clears the cell first.** See `FieldValue`: padding is compared.
void setBool(FieldValue& out, bool value) noexcept;
void setU8(FieldValue& out, core::u8 value) noexcept;
void setU16(FieldValue& out, core::u16 value) noexcept;
void setU32(FieldValue& out, core::u32 value) noexcept;
void setI32(FieldValue& out, core::i32 value) noexcept;
void setF32(FieldValue& out, float value) noexcept;
void setF64(FieldValue& out, double value) noexcept;
void setVec3(FieldValue& out, core::Vec3 value) noexcept;
void setPosition(FieldValue& out, core::DVec3 value) noexcept;
void setCFrame(FieldValue& out, const core::CFrameD& value) noexcept;
void setNetId(FieldValue& out, NetId value) noexcept;

[[nodiscard]] bool asBool(const FieldValue& value) noexcept;
[[nodiscard]] core::u32 asU32(const FieldValue& value) noexcept;
[[nodiscard]] float asF32(const FieldValue& value) noexcept;
[[nodiscard]] core::Vec3 asVec3(const FieldValue& value) noexcept;
[[nodiscard]] core::CFrameD asCFrame(const FieldValue& value) noexcept;
[[nodiscard]] NetId asNetId(const FieldValue& value) noexcept;

// How many bytes an encoding occupies on the wire.
//
// **Not `sizeof(FieldValue)`.** The cell is padded so it can be compared as
// bytes; the wire carries exactly what the encoding needs, because the whole
// point of a per-tick diff is that it is small.
[[nodiscard]] core::usize wireBytes(generated::Encoding encoding) noexcept;

// Appends one field's wire bytes. Little-endian, fixed width, no framing of its
// own -- the message around it says which field this is.
void encodeField(std::vector<core::u8>& out, generated::Encoding encoding, const FieldValue& value);

// Reads one back. Returns false when there are not enough bytes left, which is
// the only way a decoder may fail here: everything else about the layout is
// fixed by the encoding.
[[nodiscard]] bool decodeField(std::span<const core::u8> bytes, core::usize& at, generated::Encoding encoding,
                               FieldValue& out) noexcept;

} // namespace luaug::replication
