// Engine-wide scalar vocabulary. Fixed-width names are used everywhere in
// engine code so that struct layouts -- which are snapshotted, hashed, and
// compared for determinism (ADR 0025) -- never depend on platform int sizes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace luaug::core {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

} // namespace luaug::core
