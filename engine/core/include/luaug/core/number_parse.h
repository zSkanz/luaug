// Reading a decimal number out of text, correctly and independently of locale.
//
// It was json.cpp's alone until `toml.h` arrived wanting the same conversion,
// and private to `core` while those two were its only readers. The third was a
// surface shader's `LUAUG_PARAM` default (ADR 0091), which reached for
// `std::from_chars` and broke the Android build -- the failure this function
// exists to prevent -- so it is public now.
#pragma once

#include "luaug/core/types.h"

#include <string_view>

namespace luaug::core {

// `strtod` rather than `std::from_chars`: the floating-point overloads of
// from_chars are still missing from one of the standard libraries the engine
// builds against, and strtod is the correctly-rounded conversion available on
// all of them. It reads the *locale's* decimal separator, so the separator is
// substituted rather than assumed -- nothing here calls setlocale, but a
// dependency that does must not silently turn 1.5 into 1.
//
// False when the magnitude overflows to infinity. Neither format this serves can
// express that and no caller can use it, so it is a diagnostic rather than a
// HUGE_VAL that looks like a number. Underflow to zero is left alone: it is the
// value.
[[nodiscard]] bool decimalToDouble(std::string_view token, f64& out);

} // namespace luaug::core
