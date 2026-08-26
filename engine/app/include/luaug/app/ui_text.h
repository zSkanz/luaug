// The GPU half of the UI's text: the glyph atlas as a texture, and the face
// provider that lets `TextLabel.Font` name a font out of a project's content.
//
// **Here rather than in `ui`**, and that is the layering doing its job: `ui` is
// L5 and owns no GPU resources, `render` is L4 and cannot see `ui`, and neither
// of them has content mounts. The app is the only place that can see all three,
// which is exactly what an app is for.
#pragma once

#include "luaug/asset/content.h"
#include "luaug/asset/image.h"
#include "luaug/asset/texture.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
#include "luaug/rhi/device.h"
#include "luaug/ui/ui.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luaug::app {

class UiText
{
public:
    UiText() = default;
    // Waits for any decode still running, because one is writing into memory
    // this object owns. Abandoning it is a use-after-free at shutdown, which is
    // the hardest kind to attribute -- see `MeshLoader`'s own destructor, which
    // learned this after the fact.
    ~UiText();

    UiText(const UiText&) = delete;
    UiText& operator=(const UiText&) = delete;

    // Installs the face provider, so a `TextLabel.Font` naming `asset://…` can be
    // resolved out of `mounts`. Called once the mounts exist; a null `mounts`
    // uninstalls it, which is what a world going away means.
    void setMounts(const asset::ContentMounts* mounts);

    // Uploads the atlas if a glyph has been added since the last call. Cheap on
    // every frame but the few that rasterise something: it compares one version
    // number and returns.
    //
    // Called inside a frame, because an upload needs a command list.
    void sync(rhi::IDevice& device, rhi::ICmdList& cmd);

    void destroy(rhi::IDevice& device);

    // **Whether a UI picture may take more than one frame to arrive** (D128).
    //
    // A 1024-square PNG costs 14 to 36 ms to decode -- measured for D118 on a
    // material's maps, and an `ImageLabel` names the same kind of file. The
    // synchronous path decodes every image a frame newly named, in that frame,
    // with no bound: a HUD that comes up with eight icons pays all eight at once.
    //
    // Deferred, the read goes to `platform::readFileAsync`, the decode to the
    // job pool, and only the upload happens on the frame. An unresolved image
    // already draws as the flat tint everywhere in `ui`, so what deferring costs
    // is a few frames of tint rather than a hole.
    //
    // **Off by default**, for the reason every switch beside it is: a capture
    // records the frame it was told to record.
    void setDeferredImages(bool deferred) noexcept { deferredImages_ = deferred; }

    // How many pictures are on their way in. Zero in the synchronous mode.
    [[nodiscard]] core::usize imagesInFlight() const noexcept;

    // **A request, not a load.** The draw list is built before `sync` runs, and
    // an upload needs a command list -- so a URN nobody has seen is RECORDED
    // here and returns false, `sync` loads it with the device it has, and the
    // frame after that draws the picture. One frame of flat tint, which is
    // exactly what "still arriving" looks like and is the same answer a picture
    // genuinely still streaming would give.
    //
    // Public because it IS the provider contract `ui` is handed -- the thunk
    // below only forwards -- and because the state machine behind it had no test
    // at all until one could reach it.
    [[nodiscard]] bool requestImage(std::string_view urn, ui::ResolvedImage& out);

    // Invalid until something has been rasterised, which is the state a build
    // with no font file stays in -- there the built-in vector face draws solid
    // rectangles and samples nothing.
    [[nodiscard]] rhi::TextureHandle atlasTexture() const noexcept { return atlas_; }

    // Every image the UI has asked for, in the order it asked. The frame loop
    // appends these after the atlas, so index 2 is the first picture -- which
    // is the numbering `ui::ResolvedImage::texture` hands back.
    [[nodiscard]] std::span<const rhi::TextureHandle> images() const noexcept { return images_; }

private:
    const asset::ContentMounts* mounts_ = nullptr;
    rhi::TextureHandle atlas_{};
    core::u32 width_ = 0;
    core::u32 height_ = 0;
    core::u64 uploadedVersion_ = 0;
    // The atlas expanded from coverage to RGBA. Kept between frames so a
    // re-upload does not allocate four megabytes every time a new glyph appears.
    std::vector<std::byte> staging_;

    // One entry per distinct `Image` URN, in first-asked order. Never removed
    // while the world lives: a picture a HUD shows on one screen is a picture it
    // will show again, and the index handed to `ui` has to stay meaning the same
    // texture for as long as a draw list can hold it.
    // **A named state rather than two booleans** (D128).
    //
    // `pending` and `failed` were sound only because the read, the decode and
    // the upload happened between two adjacent statements: `pending = false;
    // failed = true;` assumed failure and then took it back. Split across
    // frames, an in-flight entry reads as permanently dead -- and leaving
    // `pending` true instead re-queues the same URN every frame for ever. There
    // is no pair of booleans that says "in flight"; there is a state that does.
    enum class ImageState : core::u8
    {
        // Named by a label and waiting for a slot in the pipeline.
        Requested,
        Reading,
        Decoding,
        Ready,
        // Not there, not a picture, or the device refused it. Remembered so a
        // label naming a missing picture costs one lookup rather than one
        // attempt per frame.
        Failed,
    };

    // What a decode job writes into, in ONE heap allocation.
    //
    // Not a field of `Image`, and that is the point: `imageEntries_` grows
    // whenever a label names a URN nobody has named before, which can happen on
    // the same frame a job is running -- so a job holding the address of
    // anything inside an element would be writing into freed memory after the
    // reallocation. `MeshLoader` learned this the hard way one field at a time.
    struct ImageWork
    {
        std::vector<std::byte> bytes;
        asset::Image decoded;
        asset::TextureAsset compiled;
        bool ok = false;
        bool isCompiled = false;
    };

    struct Image
    {
        std::string urn;
        rhi::TextureHandle texture{};
        core::u32 width = 0;
        core::u32 height = 0;
        ImageState state = ImageState::Requested;
        platform::IoRequest read;
        jobs::JobHandle decode;
        std::unique_ptr<ImageWork> work;
    };
    // How many may be in the pipeline at once. `MaxImages` is a lifetime cap on
    // distinct URNs and not a concurrency one; without this, a screen naming
    // three hundred icons would open three hundred files and hold three hundred
    // decoded images at the same time.
    static constexpr core::usize MaxImagesInFlight = 4;

    bool deferredImages_ = false;
    void pumpImages(rhi::IDevice& device, rhi::ICmdList& cmd);
    void releasePendingImages() noexcept;
    [[nodiscard]] bool imageInFlight(std::string_view urn) const noexcept;

    // How many reads or decodes are outstanding, so the bound above is a bound
    // and not a hope.
    core::usize bytesInFlight_ = 0;
    // Set where a handle is assigned, so the table below is rebuilt on the pass
    // that produced a texture rather than on the pass that queued one.
    bool imagesChanged_ = false;
    std::vector<Image> imageEntries_;
    std::vector<rhi::TextureHandle> images_;

    static bool requestImageThunk(void* user, std::string_view urn, ui::ResolvedImage& out);
    void loadPendingImages(rhi::IDevice& device, rhi::ICmdList& cmd);
};

} // namespace luaug::app
