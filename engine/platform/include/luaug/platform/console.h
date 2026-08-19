#pragma once

#include <string_view>

#include "luaug/core/types.h"

namespace luaug::platform
{

enum class ConsoleStream : core::u8
{
    Out,
    Err,
};

// Writes UTF-8 text to a standard stream.
//
// Exists because writing catalog bytes with fwrite is wrong on Windows: a
// console decodes what it is handed using its own codepage, so the em dash in
// an English string arrives as "ÔÇö" under CP-850 and every accented locale is
// worse. When the stream is an attached console this converts to UTF-16 and
// uses the console's own wide entry point; when it is redirected -- a pipe, a
// file, CI -- the bytes go through untouched, which is what those consumers
// expect.
//
// This replaces M0's process-wide SetConsoleOutputCP, which worked but changed
// state belonging to the user's console rather than to us.
void writeConsole(ConsoleStream stream, std::string_view utf8);

} // namespace luaug::platform
