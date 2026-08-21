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

#include <optional>

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

private:
    const asset::ContentMounts* mounts_ = nullptr;
    rhi::TextureHandle atlas_{};
    core::u32 width_ = 0;
    core::u32 height_ = 0;
    core::u64 uploadedVersion_ = 0;
    // The atlas expanded from coverage to RGBA. Kept between frames so a
    // re-upload does not allocate four megabytes every time a new glyph appears.
    std::vector<std::byte> staging_;
};

} // namespace luaug::app
