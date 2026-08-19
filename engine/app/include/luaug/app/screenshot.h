// Writing what the engine rendered to a file the agent can look at.
//
// This is the second half of "the agent needs eyes before it needs features"
// (roadmap M1). rhi::IDevice::readTexture gets the pixels off the GPU; this
// gets them onto disk in a format `tools/imgcmp` can compare against a
// reference. Everything from M4 onward gates on that comparison.
//
// It lives in `app` because the host is what takes screenshots. When `asset`
// arrives (M4) and image IO becomes a real subsystem, this moves there.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::app
{

// Writes tightly packed 8-bit RGBA as PNG. `pixels` must hold exactly
// width * height * 4 bytes, top row first.
//
// Returns the error rather than logging it: a screenshot that silently failed
// to write is indistinguishable from one that matched, and the whole point of
// the file is to be evidence.
[[nodiscard]] std::optional<core::EngineError> writePng(
    const std::filesystem::path& path, std::span<const std::byte> pixels, core::u32 width, core::u32 height);

} // namespace luaug::app
