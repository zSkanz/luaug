#pragma once

#include "luaug/core/types.h"
#include "luaug/rhi/types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::asset {
struct Image;
}

namespace luaug::rhi {
class ICmdList;
class IDevice;
} // namespace luaug::rhi

namespace luaug::app {

// **A picture's row in the content browser shows the picture.**
//
// An icon says "this is an image"; the file name already said that. What
// somebody scrolling a folder of textures is looking for is *which* image, and
// no icon can answer it -- the whole reason every asset browser worth using
// draws the thing itself.
//
// Three costs make this a cache rather than a `decodeImage` in the draw loop,
// and each one is a way the naive version fails on a real project:
//
//   - **Size.** A 4K PNG is 64 MB as RGBA8. A folder of forty is more VRAM than
//     the world it is meant to decorate, spent on pictures drawn at 96 px. So
//     what is uploaded is a downscale, at `Edge` on the longer side, and the
//     full-size decode is thrown away in the same breath it was made.
//   - **Time.** Decoding one 4K PNG is tens of milliseconds. A folder's worth in
//     one frame is a freeze, and the frame it freezes is the one where somebody
//     just clicked into the folder. So `PerFrame` of them land per frame and the
//     rest fill in behind, which reads as a browser thinking rather than as an
//     editor hanging.
//   - **Count.** Scrolling never ends. `Resident` is a hard ceiling with
//     least-recently-drawn eviction, so a project with nine thousand textures
//     costs exactly what a project with two hundred does.
//
// Requests come from the draw and the work happens in the frame loop, which is
// the same shape `UiText` uses for `ImageLabel` and for the same reason: an
// upload needs a live command list, and the panel is drawn nowhere near one. A
// picture is therefore ready on the frame AFTER it was first asked for, which is
// invisible on a panel that stays open.
class ThumbnailCache
{
public:
    // The longer side of what is uploaded. 128 keeps the largest grid cell the
    // browser offers sharp without paying for a size it never draws.
    static constexpr core::u32 Edge = 128;
    // 128x128 RGBA8 is 64 KB, so the ceiling is 16 MB of VRAM -- a fixed,
    // knowable cost that does not depend on how big the project's textures are.
    static constexpr core::usize Resident = 256;
    // Two per frame. Measured against the worst case that matters: a folder of
    // 4K PNGs fills a screenful in under a second and never drops a frame doing
    // it.
    static constexpr core::usize PerFrame = 2;

    struct Thumbnail
    {
        rhi::TextureHandle texture;
        // The DOWNSCALED size, and it keeps the source's aspect: a 16:9 texture
        // drawn into a square cell is letterboxed rather than squashed, because
        // a squashed thumbnail is a thumbnail of a different picture.
        core::u32 width = 0;
        core::u32 height = 0;

        [[nodiscard]] bool valid() const noexcept { return texture.valid() && width > 0 && height > 0; }
    };

    // What to draw for `path` on frame `frame`, and a standing request to have
    // it. Invalid until it is ready or forever if it cannot be, and the caller
    // draws the kind's icon in both cases -- which is why this never reports
    // failure: there is nothing different to do about it.
    [[nodiscard]] Thumbnail request(const std::filesystem::path& path);

    // Decodes and uploads what was asked for, up to `PerFrame`, then evicts down
    // to `Resident`. Runs where a command list is live.
    //
    // **Eviction happens here, before the panel is drawn**, and that ordering is
    // what makes it safe: what is dropped was last wanted on an earlier frame,
    // so nothing in this frame's draw list refers to it.
    void flush(rhi::IDevice& device, rhi::ICmdList& cmd);

    void destroy(rhi::IDevice& device);

    // Uploaded and drawable.
    [[nodiscard]] core::usize residentCount() const noexcept;
    // Asked for and not yet attempted -- what the next flushes will work
    // through, and what says the budget is a budget rather than a stall.
    [[nodiscard]] core::usize pendingCount() const noexcept;
    // Every path this remembers anything about, resident or failed or waiting.
    // The number `Resident` caps, and the one that says eviction happened.
    [[nodiscard]] core::usize trackedCount() const noexcept { return entries_.size(); }

private:
    struct Entry
    {
        std::string key;
        rhi::TextureHandle texture;
        core::u32 width = 0;
        core::u32 height = 0;
        core::u64 lastWanted = 0;
        bool pending = true;
        bool failed = false;
    };

    [[nodiscard]] Entry* find(std::string_view key) noexcept;

    std::vector<Entry> entries_;
    // Bumped by `flush`, so "least recently wanted" means "not asked for in
    // the most frames" without the caller having to have a frame number.
    core::u64 frame_ = 0;
};

// The resample behind a thumbnail: `source` fitted into a box of `edge` on its
// longer side, aspect kept, at least one texel on each side.
//
// **A box filter in linear light, weighted by alpha**, and both halves of that
// are corrections to what the obvious version does:
//
//   - Averaging sRGB bytes averages numbers that are not proportional to light,
//     which darkens every gradient it touches. Halving a checkerboard of black
//     and white should give 0.5 in light and reads as 188, not 128.
//   - Averaging RGB across a transparent texel drags the colour toward whatever
//     was stored where nothing is drawn -- usually black. Weighting by alpha is
//     what makes a cut-out's edge stay the colour of the cut-out.
//
// Returns false for an empty source. Exposed for tests: it is the half of a
// thumbnail that has nothing to do with a GPU.
[[nodiscard]] bool makeThumbnail(const asset::Image& source, core::u32 edge, asset::Image& out);

} // namespace luaug::app
