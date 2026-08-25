#pragma once

#include "luaug/asset/image.h"
#include "luaug/core/types.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
#include "luaug/rhi/types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
//   - **Time.** See below: it is the reason this is a pipeline.
//   - **Count.** Scrolling never ends. `Resident` is a hard ceiling with
//     least-recently-drawn eviction, so a project with nine thousand textures
//     costs exactly what a project with two hundred does.
//
// **Three stages, because the decode does not fit in a frame.** Measured on
// ordinary 1024-square PNGs from a texture pack: 14 to 36 ms to decode and 2 ms
// to resample, EACH. That is a whole frame at 60 Hz and eight at 240, so a
// budget on the main thread cannot help -- an image cannot be decoded half way,
// and a floor of one per frame is a floor of one dropped frame per thumbnail.
// At 4K it is sixteen times worse.
//
// So the file is read by `platform::readFileAsync`, the decode and the resample
// run on the job pool as `Domain::AssetIo` -- which that domain is defined as,
// "decode, transcode, materialisation preparation" -- and only the upload
// happens on the frame, because only the frame has a command list. Opening a
// folder of 4K textures costs the main thread a few small uploads and nothing
// else.
//
// A picture is therefore ready some frames after it was first asked for, and
// the row draws its icon until then. On a panel that stays open, that is a
// browser filling in rather than an editor hanging.
class ThumbnailCache
{
public:
    // The longer side of what is uploaded. 128 keeps the largest grid cell the
    // browser offers sharp without paying for a size it never draws.
    static constexpr core::u32 Edge = 128;
    // 128x128 RGBA8 is 64 KB, so the ceiling is 16 MB of VRAM -- a fixed,
    // knowable cost that does not depend on how big the project's textures are.
    static constexpr core::usize Resident = 256;
    // How many files may be in the pipeline at once. Bounded because a folder of
    // nine hundred textures would otherwise queue nine hundred reads and hold
    // nine hundred decoded images in memory at the same time; four is enough to
    // keep the pool fed while a person is still reading the first screenful.
    static constexpr core::usize MaxInFlight = 4;

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

    ThumbnailCache() = default;
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // What to draw for `path`, and a standing request to have it. Invalid until
    // it is ready or forever if it cannot be, and the caller draws the kind's
    // icon in both cases -- which is why this never reports failure: there is
    // nothing different to do about it.
    [[nodiscard]] Thumbnail request(const std::filesystem::path& path);

    // One frame of the pipeline: admit what fits, take what the IO service
    // finished, upload what the pool finished, then evict down to `Resident`.
    // Runs where a command list is live.
    //
    // **Eviction happens here, before the panel is drawn**, and that ordering is
    // what makes it safe: what is dropped was last wanted on an earlier frame,
    // so nothing in this frame's draw list refers to it.
    void flush(rhi::IDevice& device, rhi::ICmdList& cmd);

    // Waits for what is in flight and frees every texture. The wait is not
    // optional: a decode job holds a pointer into this object, and returning
    // while one is running would free the memory it is writing into.
    void destroy(rhi::IDevice& device);

    // Uploaded and drawable.
    [[nodiscard]] core::usize residentCount() const noexcept;
    // Asked for and not yet drawable -- queued, reading, or decoding. What says
    // the pipeline is a pipeline rather than a stall.
    [[nodiscard]] core::usize pendingCount() const noexcept;
    // Every path this remembers anything about, resident or failed or waiting.
    // The number `Resident` caps, and the one that says eviction happened.
    [[nodiscard]] core::usize trackedCount() const noexcept { return entries_.size(); }

private:
    enum class Stage : core::u8
    {
        // Asked for, and waiting for a slot in the pipeline.
        Queued,
        Reading,
        Decoding,
        Ready,
        // Not there, not a picture, or the device refused it. Remembered so a
        // folder holding one bad file does not spend a slot on it every frame.
        Failed,
    };

    // The state a decode job writes into. Held by pointer so its address is
    // stable while `entries_` grows underneath it -- a job holding an index into
    // a vector that reallocates is a job writing into freed memory.
    struct Work
    {
        std::vector<std::byte> bytes;
        asset::Image image;
        bool ok = false;
    };

    struct Entry
    {
        std::string key;
        rhi::TextureHandle texture;
        core::u32 width = 0;
        core::u32 height = 0;
        core::u64 lastWanted = 0;
        Stage stage = Stage::Queued;
        platform::IoRequest read;
        jobs::JobHandle decode;
        std::unique_ptr<Work> work;
    };

    [[nodiscard]] Entry* find(std::string_view key) noexcept;
    [[nodiscard]] core::usize inFlight() const noexcept;
    void admit();
    void collectReads();
    void collectDecodes(rhi::IDevice& device, rhi::ICmdList& cmd);
    void evict(rhi::IDevice& device);

    std::vector<Entry> entries_;
    // Bumped by `flush`, so "least recently wanted" means "not asked for in the
    // most frames" without the caller having to have a frame number.
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
