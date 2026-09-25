// A `Sky`'s six images, read, resampled and uploaded without the frame waiting
// (ADR 0096).
//
// **Nothing here runs on the frame thread but a poll and one upload.** When
// the faces or the orientation change, six jobs read and decode the pictures,
// eight resample bands of the octahedral image once all six are in, and a last
// one averages it into the linear copy the environment reads. The frame asks
// `jobs::finished` of that last one and, when it says yes, uploads the result.
// Until then **the previous sky keeps drawing** -- a sky that went black while
// it loaded would be worse than one a moment out of date.
//
// A headless run waits for the bake instead, so the frame a golden is taken
// from is the sky that was asked for rather than whichever frame it was ready
// by (`setSynchronous`).
#pragma once

#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/jobs/jobs.h"
#include "luaug/render/look.h"
#include "luaug/render/skybox.h"
#include "luaug/rhi/device.h"

#include <filesystem>
#include <memory>

namespace luaug::asset {
class ContentMounts;
}

namespace luaug::scene {
class World;
}

namespace luaug::render {

struct RenderWorld;

class SkyLoader
{
public:
    SkyLoader();
    ~SkyLoader();
    SkyLoader(const SkyLoader&) = delete;
    SkyLoader& operator=(const SkyLoader&) = delete;

    void setContentRoot(std::filesystem::path root) { contentRoot_ = std::move(root); }
    void setContentMounts(const asset::ContentMounts* mounts) noexcept { mounts_ = mounts; }
    // Wait for a bake rather than drawing the previous sky meanwhile.
    void setSynchronous(bool synchronous) noexcept { synchronous_ = synchronous; }

    // Notices the sky `look` asks for, starts a bake when it changed, and
    // uploads a finished one. Before any render pass: it may upload.
    void sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, const RenderSky& look);

    // Hands the current picture to the frame: its texture for the sky pass and
    // its linear copy for the environment.
    void append(RenderWorld& out) const;

    // How long the last bake took on its jobs, in milliseconds -- for the
    // performance ledger's `Sky` row. Zero before the first.
    [[nodiscard]] core::f64 lastBakeMilliseconds() const noexcept { return lastBakeMs_; }

    void destroy(rhi::IDevice& device);

private:
    struct Key
    {
        core::NameAtom faces[kSkyFaceCount]{};
        core::Vec3 orientation{};
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };
    struct Bake;

    void start(const scene::World& world, const Key& key);

    std::filesystem::path contentRoot_;
    const asset::ContentMounts* mounts_ = nullptr;
    bool synchronous_ = false;

    // What was asked for last, what is drawing now, and the bake in between.
    Key wanted_{};
    bool wantedAny_ = false;
    std::unique_ptr<Bake> bake_;
    rhi::TextureHandle texture_{};
    std::shared_ptr<const SkyRadiance> radiance_;
    core::f64 lastBakeMs_ = 0.0;
};

} // namespace luaug::render
