#pragma once

// Where the renderer gets a surface shader it did not ship (ADR 0091).
//
// A surface the engine ships is compiled at the engine's build and loaded by
// name from `content/shaders/`. One a USER wrote is named by URN, and its
// bytecode comes from somebody else: the editor's compiler, which builds it on
// a worker while the frame goes on, or -- in a packaged game -- the pack, which
// carries bytecode `assetc` built. The renderer asks every frame and never
// waits: a surface that is not ready draws with the built-in surface this frame
// and is asked for again the next.

#include "luaug/asset/surface_shader.h"
#include "luaug/core/types.h"
#include "luaug/rhi/types.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace luaug::render {

// One surface, ready to build pipelines from: its layout and the bytecode of
// every variant's two stages, in the format the device asked for.
struct SurfaceProgram
{
    asset::SurfaceReflection reflection;
    // `[variant * 2 + stage]`, in `asset::SurfaceVariant` order, vertex first.
    std::array<std::vector<std::byte>, 10> code;
    // Bumped whenever the program changes -- a saved shader, a saved include --
    // so the renderer rebuilds its pipelines rather than keeping stale ones.
    core::u64 revision = 0;
};

enum class SurfaceStatus : core::u8
{
    // `program` is set.
    Ready,
    // Being compiled, or not asked for before: draw the built-in surface.
    Pending,
    // It does not compile, or cannot be found: draw the error surface.
    Failed,
};

class ISurfaceSource
{
public:
    virtual ~ISurfaceSource() = default;

    // What `urn` is, for a device that consumes `format`. Called from the
    // render thread every frame a material names it, so it must not block:
    // a source that has not started compiling starts, and answers Pending.
    [[nodiscard]] virtual SurfaceStatus find(std::string_view urn, rhi::ShaderFormat format,
                                             const SurfaceProgram*& program) = 0;
};

} // namespace luaug::render
