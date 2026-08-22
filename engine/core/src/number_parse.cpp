#include "number_parse.h"

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <string>

namespace luaug::core::detail {

bool decimalToDouble(std::string_view token, f64& out)
{
    std::string buffer(token);

    const char* separator = std::localeconv()->decimal_point;
    if (separator != nullptr && std::string_view{separator} != ".") {
        const usize dot = buffer.find('.');
        if (dot != std::string::npos)
            buffer.replace(dot, 1, separator);
    }

    // The WHOLE token has to convert. `strtod` stops at the first character it
    // cannot use and reports success for what it read, so `1979-05-27` comes
    // back as 1979 -- which is a date silently becoming a number, and is exactly
    // what a reader that refuses what it does not understand must not do.
    char* end = nullptr;
    const f64 value = std::strtod(buffer.c_str(), &end);
    if (end != buffer.c_str() + buffer.size())
        return false;
    if (std::isinf(value))
        return false;

    out = value;
    return true;
}

} // namespace luaug::core::detail
