// Enum reflection (api-design.md §2.3, ADR 0016).
//
// Generated from `api/defs/enums.api.luau` by the same pass that emits the
// class descriptors, for the same reason: the `Enum` global a script reads, the
// `.d.luau` the analyzer reads and the validation a property write performs are
// three views of one list, and they only agree because none of them is written
// twice.
//
// A `Value` stores an enum as `{enumId, value}` and never as a pointer, because
// it has to survive a snapshot and a restore. That makes this registry the only
// thing that can turn either half back into a name -- which is exactly what the
// `EnumItem` binding does on the way out to Luau.
#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "luaug/core/name_atom.h"
#include "luaug/core/text_key.h"
#include "luaug/scene/types.h"
#include "luaug/scene/value.h"

namespace luaug::scene
{

// 0 is "no enum", so a zero-initialised `EnumValue` is detectably empty rather
// than accidentally the first enum -- the same convention `ClassId`,
// `InstanceId` and `NameAtom` use.
using EnumId = u16;
inline constexpr EnumId InvalidEnum = 0;

struct EnumItemDesc
{
    core::NameAtom name;
    // Contract, not bookkeeping: this is what serialization writes and what a
    // severity comparison reads (`enums.api.luau`). It is not the index.
    i32 value = 0;
    core::TextKey docKey;
};

struct EnumDescriptor
{
    core::NameAtom name;
    core::TextKey docKey;
    // A view into generated static storage, which outlives the registry. Items
    // stay in declaration order, which is `GetEnumItems`'s documented order and
    // therefore something R10 forbids a hash container from deciding.
    std::span<const EnumItemDesc> items;
};

class EnumRegistry
{
public:
    // Not defaulted: the constructor seeds the unused slot 0 that keeps
    // `EnumId` 0 meaning "no enum".
    EnumRegistry();

    // Returns `InvalidEnum` if the name is already taken.
    EnumId registerEnum(const EnumDescriptor& descriptor);

    [[nodiscard]] const EnumDescriptor* find(EnumId id) const noexcept;
    [[nodiscard]] EnumId findId(core::NameAtom name) const noexcept;

    // Nullptr for a name this enum does not have. Linear over the items, and
    // deliberately: no enum in the v1 surface has more than a handful, and a
    // hash map per enum would cost more to build than it could ever save.
    [[nodiscard]] const EnumItemDesc* findItem(EnumId id, core::NameAtom itemName) const noexcept;

    // What a property write validates against: an `EnumValue` carrying a number
    // no item has is a value nothing in the enum means.
    [[nodiscard]] const EnumItemDesc* findValue(EnumId id, i32 value) const noexcept;

    [[nodiscard]] usize enumCount() const noexcept { return m_enums.size(); }

private:
    // Index 0 is unused so that `EnumId` 0 stays invalid.
    std::vector<EnumDescriptor> m_enums;
    std::unordered_map<u32, EnumId> m_byName;
};

} // namespace luaug::scene
