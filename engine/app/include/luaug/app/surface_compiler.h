#pragma once

// The editor's surface shader compiler (ADR 0091, stage 4).
//
// **Asynchronous, cached and loud.** The renderer asks for a surface by URN
// every frame; the first ask starts a compile on a worker and answers Pending,
// so the part draws with the built-in surface and no frame waits. The compile
// runs `shadercross` -- the program the engine's own build runs, found beside
// the editor or in the build's host tools -- once per pass variant and stage,
// over the wrappers `asset::surfaceWrapper` writes. The result is cached by a
// hash of everything that can change it: the source, every file it includes,
// the contract version and the bytecode format -- in memory, and on disk so a
// second session starts warm.
//
// A shader that does not compile answers Failed -- the error surface -- and
// every error is logged with its file and line, which is what the editor's
// console shows. A saved shader or include is seen within half a second and
// recompiled; the renderer rebuilds its pipelines when the revision moves.

#include "luaug/asset/content.h"
#include "luaug/render/surface_source.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace luaug::app {

// One compile error, where the compiler put it.
struct SurfaceError
{
    std::string file;
    core::u32 line = 0;
    std::string message;
};

class SurfaceCompiler final : public render::ISurfaceSource
{
public:
    // `shadercross` is the compiler to run; `includeDirectory` holds the
    // engine's headers (`luaug/surface.hlsli` and what it builds on); `cache`
    // is where wrappers and bytecode are kept between sessions.
    SurfaceCompiler(const asset::ContentMounts& mounts, std::filesystem::path shadercross,
                    std::filesystem::path includeDirectory, std::filesystem::path cache);
    ~SurfaceCompiler() override;
    SurfaceCompiler(const SurfaceCompiler&) = delete;
    SurfaceCompiler& operator=(const SurfaceCompiler&) = delete;

    [[nodiscard]] render::SurfaceStatus find(std::string_view urn, rhi::ShaderFormat format,
                                             const render::SurfaceProgram*& program) override;

    // The errors of the last compile of `urn`, empty when it compiled.
    [[nodiscard]] std::vector<SurfaceError> errors(std::string_view urn) const;
    // Where `urn` stands, without asking for it: nothing when it has never been
    // asked for -- a shader no drawn material names is compiled by nobody.
    [[nodiscard]] std::optional<render::SurfaceStatus> status(std::string_view urn) const;

    // Whether a compiler was found at all. An editor without one draws every
    // user surface as the error surface and says why once.
    [[nodiscard]] bool available() const noexcept;

    // Blocks until nothing is compiling: for tests and for a measured compile.
    void drain();

private:
    struct Entry
    {
        std::string urn;
        rhi::ShaderFormat format = rhi::ShaderFormat::Unknown;
        render::SurfaceStatus status = render::SurfaceStatus::Pending;
        bool queued = false;
        std::unique_ptr<render::SurfaceProgram> program;
        std::vector<SurfaceError> errors;
        // What it was built from, with the times they were last written.
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> dependencies;
        std::chrono::steady_clock::time_point checked{};
        core::u64 revision = 0;
    };

    void work();
    void compile(const std::string& urn, rhi::ShaderFormat format);

    const asset::ContentMounts& m_mounts;
    std::filesystem::path m_shadercross;
    std::filesystem::path m_include;
    std::filesystem::path m_cache;
    // A hash of every engine header, part of every cache key.
    core::u64 m_headers = 0;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::map<std::string, Entry, std::less<>> m_entries;
    std::deque<std::string> m_queue;
    bool m_busy = false;
    bool m_stop = false;
    bool m_warned = false;
    std::thread m_worker;
};

} // namespace luaug::app
