#pragma once

// **Compiling a surface shader**, and the form a compiled one takes in a pack
// (ADR 0091).
//
// One routine for both callers: the editor's `SurfaceCompiler`, which runs it
// on a worker as a file is saved, and `assetc`, which runs it once per target
// when a project is built into a pack. Two copies of "wrap it, run
// shadercross, read the bytes back" would be two answers to what a surface
// compiles to -- and the pack is the one a player runs.
//
// It runs a process, which is why it is here and not in `surface_shader.h`:
// that header is the contract and its text, and is included by code that must
// never start one.

#include "luaug/asset/surface_shader.h"
#include "luaug/core/types.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luaug::asset {

// The bytecode a surface is compiled to. The asset layer's own names:
// `rhi::ShaderFormat` is a layer above this one.
enum class SurfaceTarget : core::u8
{
    Spirv,
    Dxil,
    Msl,
};

inline constexpr std::array<SurfaceTarget, 3> AllSurfaceTargets{SurfaceTarget::Spirv, SurfaceTarget::Dxil,
                                                                SurfaceTarget::Msl};

// What shadercross calls it: `SPIRV`, `DXIL`, `MSL`.
[[nodiscard]] std::string_view surfaceTargetName(SurfaceTarget target) noexcept;

// `[variant * 2 + stage]`, in `SurfaceVariant` order, vertex first: five
// variants, two stages.
inline constexpr core::usize SurfaceCodeSlots = 10;
using SurfaceCode = std::array<std::vector<std::byte>, SurfaceCodeSlots>;

// One problem, where the compiler or the reflection put it. `file` is empty
// when the compiler named no file; `line` is one-based, zero for none.
struct SurfaceBuildError
{
    std::string file;
    core::u32 line = 0;
    std::string message;
};

struct SurfaceBuildInputs
{
    // The `.surface.hlsl`.
    std::filesystem::path source;
    // The shadercross executable.
    std::filesystem::path shadercross;
    // The engine's headers: the directory `luaug/surface.hlsli` is under.
    std::filesystem::path include;
    // Where the wrappers are written and the bytecode is kept, a directory per
    // key. Required: shadercross reads a file, so there is always somewhere.
    std::filesystem::path cache;
    // `surfaceHeadersHash(include)`, computed once by the caller -- the headers
    // change with the engine, not between two shaders.
    core::u64 headers = 0;
};

struct SurfaceBuild
{
    bool ok = false;
    SurfaceReflection reflection;
    SurfaceCode code;
    std::vector<SurfaceBuildError> errors;
    // The source and every project include it reached, for a caller that
    // watches them.
    std::vector<std::filesystem::path> files;
    // How many stages the compiler actually ran for; zero is a cache hit.
    core::u32 compiled = 0;
};

// A hash of every file under `include`, names and bytes.
[[nodiscard]] core::u64 surfaceHeadersHash(const std::filesystem::path& include);

// Wraps, compiles and reads back every stage of every variant for `target`.
// **Cached by what it was built from** -- the contract version, the target,
// the source and its includes, the generated wrappers and the engine's
// headers -- so a second build of the same shader runs nothing.
[[nodiscard]] SurfaceBuild buildSurface(const SurfaceBuildInputs& inputs, SurfaceTarget target);

// shadercross's output, as errors: every `file:line:column: error: text`.
[[nodiscard]] std::vector<SurfaceBuildError> parseSurfaceErrors(std::string_view output);

// --- In a pack (`AssetKind::Surface`) ----------------------------------------
//
// The source, so a player reflects the same parameters the editor did, and the
// bytecode for each target the build could compile. A target that is missing
// is a build that had no compiler for it, and a player on that backend draws
// the error surface and says why.
struct CompiledSurface
{
    std::string source;
    std::vector<std::pair<SurfaceTarget, SurfaceCode>> targets;

    [[nodiscard]] const SurfaceCode* code(SurfaceTarget target) const noexcept;
};

inline constexpr core::u32 CompiledSurfaceVersion = 1;

[[nodiscard]] std::vector<std::byte> encodeSurface(const CompiledSurface& surface);
// Nothing for bytes that are not one, or were written for another contract.
[[nodiscard]] std::optional<CompiledSurface> decodeSurface(std::span<const std::byte> bytes);

} // namespace luaug::asset
