// The GPU half of the UI's text: the glyph atlas as a texture, and the face
// provider that lets `TextLabel.Font` name a font out of a project's content.
//
// **Here rather than in `ui`**, and that is the layering doing its job: `ui` is
// L5 and owns no GPU resources, `render` is L4 and cannot see `ui`, and neither
// of them has content mounts. The app is the only place that can see all three,
// which is exactly what an app is for.
#pragma once

#include "luaug/asset/content.h"
#include "luaug/rhi/device.h"
#include "luaug/ui/ui.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luaug::app {

class UiText
{
public:
    UiText() = default;
    ~UiText() = default;

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
    struct Image
    {
        std::string urn;
        rhi::TextureHandle texture{};
        core::u32 width = 0;
        core::u32 height = 0;
        // Asked for and not there. Remembered so a label naming a missing
        // picture costs one lookup rather than one decode attempt per frame.
        bool failed = false;
        // Asked for and not yet loaded. Cleared by the first `sync` that sees
        // it, which is the frame after the label first named it.
        bool pending = true;
    };
    std::vector<Image> imageEntries_;
    std::vector<rhi::TextureHandle> images_;

    // **A request, not a load.** The draw list is built before `sync` runs, and
    // an upload needs a command list -- so a URN nobody has seen is RECORDED
    // here and returns false, `sync` loads it with the device it has, and the
    // frame after that draws the picture. One frame of flat tint, which is
    // exactly what "still arriving" looks like and is the same answer a picture
    // genuinely still streaming would give.
    [[nodiscard]] bool requestImage(std::string_view urn, ui::ResolvedImage& out);
    static bool requestImageThunk(void* user, std::string_view urn, ui::ResolvedImage& out);
    void loadPendingImages(rhi::IDevice& device, rhi::ICmdList& cmd);
};

} // namespace luaug::app
