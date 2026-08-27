#pragma once

#include "luaug/asset/image.h"
#include "luaug/asset/model.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
#include "luaug/render/render_world.h"
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

// **What a row in the content browser is a picture OF.**
//
// Not `ContentKind`, and the difference is the whole reason this enum exists: a
// browser has eight kinds because it does eight different things with a file,
// and a picture has three because there are three ways of MAKING one. A scene
// and a stamp are opened and placed respectively -- two kinds to the browser --
// and both are drawn by pointing a camera at a subtree, which is one kind here.
enum class PreviewKind : core::u8
{
    // Nothing to draw: a folder, a sound, a font, a chunk, a file this build
    // does not recognise. The row wears its class icon, and this cache never
    // opens the file -- which is the point of answering the question up front
    // rather than discovering it after a read.
    None,
    // Decoded and downscaled. The path this cache was born doing.
    Texture,
    // Geometry, drawn from `previewView`.
    Mesh,
    // A scene or a stamp: the same view over the whole subtree's bounds.
    Subtree,
};

// Which of the three `path` is, by extension. Delegates to `contentKindOf`, so
// the browser and this cache cannot come to disagree about what a `.gltf` is.
[[nodiscard]] PreviewKind previewKindOf(const std::filesystem::path& path) noexcept;

// **The view every rendered preview is drawn from: a fixed three-quarter, framed
// to `bounds`.**
//
// Fixed is the requirement, not the shortcut. A thumbnail drawn through the
// scene's camera changes when the camera moves, which means a folder of models
// looks different every time it is opened and two people looking at the same
// project see different pictures of the same file. So the view is a pure
// function of the asset's own bounds and of nothing else, and this is that
// function -- one definition, used by both rendered kinds, because "framed to
// its bounds" stated twice is two answers the first time either is touched.
//
// **Three-quarter, which is the convention rather than a preference**: above, in
// front and to one side, so a box reads as a box and a character reads as facing
// somewhere. Every asset browser worth using draws models this way, for the
// reason a straight-on view of a cube is a square.
//
// **Framed against the bounding SPHERE, not the box.** A sphere subtends the
// same angle from every direction, so the distance cannot depend on which way
// the fixed view happens to point and a model cannot fall out of frame because
// it is long along the axis the camera looks down. It costs a little empty space
// around a flat asset, which is the right way to be wrong.
//
// Assumes a SQUARE target -- vertical and horizontal field of view are equal at
// an aspect of one, so one distance frames both. `ThumbnailCache::Edge` by
// `Edge` is that target.
//
// An empty box -- a mesh with no vertices, a subtree with no parts -- is framed
// as a unit box at the origin. `center` and `size` of an empty `AABB` are built
// from infinities and produce NaN, and a camera full of NaN is not recoverable;
// a preview of nothing should be a picture of nothing.
[[nodiscard]] render::ViewOverride previewView(const core::AABB& bounds) noexcept;

// Everything the render half is handed, once this cache has done every part of a
// preview that does not need a device.
struct PreviewJob
{
    PreviewKind kind = PreviewKind::None;
    // The file it came from. For a `Mesh` this is what the importer already
    // resolved external buffers and images against; for a `Subtree` it is what a
    // relative name inside the text resolves against, and it is the name to put
    // in a log either way.
    std::filesystem::path path;
    // `Mesh`: the imported model, parsed off the frame. Null for a `Subtree`.
    //
    // Borrowed for the duration of the call and no longer. The cache frees it as
    // soon as `drawPreview` returns, because a parsed glTF is the largest thing
    // in this pipeline and nothing needs it twice.
    const asset::Model* model = nullptr;
    // `Subtree`: the scene or stamp text exactly as it was read, and empty for a
    // `Mesh`. Text rather than a materialised tree because building one needs a
    // world, a world needs registries, and both of those belong to the host --
    // this cache has neither, and inventing a second set would be a second
    // definition of what a scene means.
    std::string_view text;
    // The longer side of the picture wanted, in pixels: `ThumbnailCache::Edge`.
    core::u32 edge = 0;
};

// What one came out as. The picture keeps the source's aspect for the same
// reason a texture's downscale does, which for a square-framed preview means
// `edge` by `edge` -- reported rather than assumed, so a renderer that
// letterboxes something can say so and the browser will centre it.
struct PreviewResult
{
    rhi::TextureHandle texture;
    core::u32 width = 0;
    core::u32 height = 0;
};

// **The seam the render half plugs into.**
//
// A preview is a render, and a render needs a device, a command list, a pass and
// a set of pipelines -- all of which live in `render`, which this module may
// call and does not own. So the pipeline that FEEDS a preview is here (the
// request, the read off the frame, the parse off the frame, the budget, the
// keying, the cache) and the pass that DRAWS one is behind this interface.
//
// The contract, in the order it matters:
//
//   - **Called from `ThumbnailCache::flush`**, which is the frame's upload
//     window: a live command list, no render pass open. The implementation opens
//     and closes its own, exactly as `IRenderer::render` does and for the same
//     reason -- whatever runs after it in the frame expects none open.
//   - **At most `MaxPreviewsPerFrame` calls per `flush`.** See that constant: a
//     preview is an upload plus a pass, and neither can be split.
//   - **Frame it with `previewView`** -- over `model->mesh.bounds` for a `Mesh`,
//     and over the materialised subtree's extents for a `Subtree`. A subtree is
//     built at the origin, so the f64 answer `scene::worldExtents` gives
//     converts to the f32 box this takes without losing anything.
//   - **One neutral light, and the project's `Lighting` is not consulted.** A
//     preview that inherited the scene's sun would change when somebody dragged
//     a slider in a panel three tabs away, which is the same defect as a preview
//     that follows the camera.
//   - **The texture belongs to the CACHE from the moment this returns true.** It
//     is destroyed on eviction and at `destroy`, so it must be created on
//     `device` and it must be `Sampled` -- the browser draws it.
//   - Returning false means "there will never be a picture of this file": the
//     entry is remembered as failed and the file is not opened again. A renderer
//     that is merely busy must not report it.
class IPreviewRenderer
{
public:
    virtual ~IPreviewRenderer() = default;

    IPreviewRenderer(const IPreviewRenderer&) = delete;
    IPreviewRenderer& operator=(const IPreviewRenderer&) = delete;

    [[nodiscard]] virtual bool drawPreview(rhi::IDevice& device, rhi::ICmdList& cmd, const PreviewJob& job,
                                           PreviewResult& out) = 0;

protected:
    IPreviewRenderer() = default;
};

// **A picture's row in the content browser shows the picture.**
//
// An icon says "this is an image"; the file name already said that. What
// somebody scrolling a folder of textures is looking for is *which* image, and
// no icon can answer it -- the whole reason every asset browser worth using
// draws the thing itself. **The same sentence is true of a folder of forty
// models**, and it is worse there: forty meshes wear one `ContentMesh` icon, so
// the panel says nothing at all except how many files there are.
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
// **Three stages, because the work does not fit in a frame.** Measured on
// ordinary 1024-square PNGs from a texture pack: 14 to 36 ms to decode and 2 ms
// to resample, EACH. That is a whole frame at 60 Hz and eight at 240, so a
// budget on the main thread cannot help -- an image cannot be decoded half way,
// and a floor of one per frame is a floor of one dropped frame per thumbnail. At
// 4K it is sixteen times worse. A mesh is worse again: 21 ms to read and **191
// ms to parse** the 2937 KiB glTF D125 was opened for.
//
// So the file is read by `platform::readFileAsync`, the decode -- an image
// resampled, or a glTF imported -- runs on the job pool as `Domain::AssetIo`,
// which is what that domain is defined as ("decode, transcode, materialisation
// preparation"), and only the upload happens on the frame, because only the
// frame has a command list. Opening a folder of 4K textures costs the main
// thread a few small uploads and nothing else.
//
// **A rendered preview adds a fourth stage and a budget of its own**, because it
// is the one kind whose expensive half genuinely cannot leave the frame: see
// `MaxPreviewsPerFrame` and `IPreviewRenderer`.
//
// A picture is therefore ready some frames after it was first asked for, and the
// row draws its icon until then. On a panel that stays open, that is a browser
// filling in rather than an editor hanging.
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
    //
    // **A preview waiting for its turn at the frame still counts**, which is
    // what keeps four the true bound on resident work: an imported glTF is tens
    // of megabytes of vertices, and a queue of them waiting on a budget of one
    // per frame would otherwise grow to the size of the folder.
    static constexpr core::usize MaxInFlight = 4;

    // **One rendered preview a frame, and one is not a placeholder for a
    // measurement.**
    //
    // A preview is a whole mesh uploaded and a whole pass drawn over it, and
    // neither can be split: any budget needs a floor of one whole preview, and
    // one whole preview is already the unit. `MeshLoader` lands one mesh per
    // call for exactly this reason (D125), and the half of that cost this
    // pipeline cannot move off the frame -- the upload and the draw -- is
    // precisely what is left here.
    //
    // What it buys is that a folder of forty models costs forty frames rather
    // than one frame forty times as long. That is the promise the texture path
    // already makes and the one `MeshLoader` makes; three pipelines answering
    // "how much of this may land at once" differently would be three ways for
    // one folder to stall the editor.
    static constexpr core::usize MaxPreviewsPerFrame = 1;

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

    // Who draws a mesh, a scene and a stamp. Not owned, and it must outlive this
    // cache -- the host owns both and lets go of them in the same breath.
    //
    // **Null is a supported state, not a half-configured one.** A build with no
    // preview renderer -- a headless run, a capture harness, an editor whose
    // renderer failed to create -- refuses a mesh at `request` and never opens
    // the file, which is exactly what the browser did before previews existed:
    // the row wears its icon.
    //
    // Setting one forgets the previews refused for that reason, so a renderer
    // that arrives after the browser has already drawn a frame still answers.
    // Without that, the order of two lines in the host's startup would decide
    // whether a whole folder ever gets pictures, silently.
    void setPreviewRenderer(IPreviewRenderer* renderer) noexcept;

    // What to draw for `path`, and a standing request to have it. Invalid until
    // it is ready or forever if it cannot be, and the caller draws the kind's
    // icon in both cases -- which is why this never reports failure: there is
    // nothing different to do about it.
    [[nodiscard]] Thumbnail request(const std::filesystem::path& path);

    // One frame of the pipeline: take what the IO service finished, upload what
    // the pool finished, draw what fits in the frame's preview budget, admit
    // what fits in the pipeline, then evict down to `Resident`. Runs where a
    // command list is live and no render pass is open.
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
    // Asked for and not yet drawable -- queued, reading, decoding, or waiting
    // for a turn at the frame's preview budget. What says the pipeline is a
    // pipeline rather than a stall.
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
        // Off the frame, on the job pool: a texture resampled, a mesh imported.
        // A subtree has nothing to do here -- its text goes to the renderer as
        // it was read -- and passes straight from `Reading` to `Drawing`.
        Decoding,
        // **The state a pair of booleans could not hold** (D128). Everything
        // that can be done without a device is done, and this is a preview
        // waiting its turn at the frame's budget. A stage of its own rather than
        // a flag on `Decoding` because the two are bounded by different things
        // -- one by the job pool, one by the frame -- and a machine that cannot
        // say which of them an entry is waiting on is one nobody can debug from
        // a counter.
        Drawing,
        Ready,
        // Not there, not a picture, or the device refused it. Remembered so a
        // folder holding one bad file does not spend a slot on it every frame.
        Failed,
    };

    // The state a job writes into. Held by pointer so its address is stable
    // while `entries_` grows underneath it -- a job holding an index into a
    // vector that reallocates is a job writing into freed memory.
    struct Work
    {
        std::vector<std::byte> bytes;
        // `Texture`.
        asset::Image image;
        // `Mesh`. Beside the image rather than in a union with it: this pipeline
        // is `MaxInFlight` entries deep by construction, so the unused half
        // costs four empty vectors, and a union of two types that own heap
        // memory is a lifetime question spanning two threads for no measurable
        // gain.
        asset::Model model;
        // What the importer resolves a glTF's external buffers and images
        // against. Here rather than captured by the job body, because a job body
        // must be trivially copyable and a path is not.
        std::filesystem::path baseDirectory;
        bool ok = false;
    };

    struct Entry
    {
        std::string key;
        rhi::TextureHandle texture;
        core::u32 width = 0;
        core::u32 height = 0;
        core::u64 lastWanted = 0;
        PreviewKind kind = PreviewKind::None;
        Stage stage = Stage::Queued;
        platform::IoRequest read;
        jobs::JobHandle decode;
        std::unique_ptr<Work> work;
    };

    [[nodiscard]] Entry* find(std::string_view key) noexcept;
    [[nodiscard]] core::usize inFlight() const noexcept;
    void admit();
    void collectDraws(rhi::IDevice& device, rhi::ICmdList& cmd);
    void collectReads();
    void collectDecodes(rhi::IDevice& device, rhi::ICmdList& cmd);
    void drawPreviews(rhi::IDevice& device, rhi::ICmdList& cmd);
    void evict(rhi::IDevice& device);

    std::vector<Entry> entries_;
    IPreviewRenderer* preview_ = nullptr;
    // Bumped by `flush`, so "least recently wanted" means "not asked for in the
    // most frames" without the caller having to have a frame number.
    IPreviewRenderer* previews_ = nullptr;
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
