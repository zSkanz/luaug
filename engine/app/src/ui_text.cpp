#include "luaug/app/ui_text.h"

#include "luaug/platform/file.h"
#include "luaug/ui/ui.h"

#include <cstring>

namespace luaug::app {
namespace {

// Resolves a `TextLabel.Font` to bytes out of the project's content.
//
// A free function with a `void*` because `ui::FaceProvider` is a C-style
// callback: `ui` is handed a pointer and a function and knows nothing about
// either, which is what keeps a content system out of a layout module.
bool resolveFace(void* user, std::string_view name, std::vector<core::u8>& out)
{
    const auto* const mounts = static_cast<const asset::ContentMounts*>(user);
    if (mounts == nullptr) {
        return false;
    }

    const asset::ResolvedContent resolved = mounts->resolve(name);
    if (!resolved.bytes.empty()) {
        // Out of a pack: the bytes are already resident, so this is a copy and
        // not a read.
        out.resize(resolved.bytes.size());
        std::memcpy(out.data(), resolved.bytes.data(), resolved.bytes.size());
        return true;
    }
    if (resolved.source != asset::ResolvedContent::Source::Loose) {
        return false;
    }

    std::vector<std::byte> bytes;
    if (!platform::readFile(resolved.path, bytes)) {
        return false;
    }
    // Read SYNCHRONOUSLY, and that is defensible where the streaming pump's
    // reads are not: a face is loaded once, the first time a label names it, and
    // the alternative is a frame of text in the wrong font while an async read
    // lands. A font is small and a font is needed now.
    out.resize(bytes.size());
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

} // namespace

void UiText::setMounts(const asset::ContentMounts* mounts)
{
    if (mounts_ == mounts) {
        return;
    }
    mounts_ = mounts;
    // Installing a provider drops every loaded face (`ui::setFaceProvider`), so
    // this must not run every frame -- hence the identity check above.
    ui::setFaceProvider(mounts != nullptr ? &resolveFace : nullptr, const_cast<asset::ContentMounts*>(mounts));
}

void UiText::sync(rhi::IDevice& device, rhi::ICmdList& cmd)
{
    const ui::GlyphAtlas source = ui::glyphAtlas();
    if (source.pixels.empty() || source.width == 0 || source.height == 0) {
        return;
    }
    if (atlas_.valid() && source.version == uploadedVersion_ && source.width == width_ && source.height == height_) {
        return;
    }

    if (!atlas_.valid() || source.width != width_ || source.height != height_) {
        if (atlas_.valid()) {
            device.destroy(atlas_);
        }
        atlas_ = device.createTexture({
            .format = rhi::TextureFormat::Rgba8Unorm,
            .usage = rhi::TextureUsage::Sampled,
            .width = source.width,
            .height = source.height,
            .debugName = "ui-glyph-atlas",
        });
        width_ = source.width;
        height_ = source.height;
        if (!atlas_.valid()) {
            return;
        }
    }

    // **RGBA rather than R8**, and the reason is that there is one UI shader.
    //
    // A glyph is coverage, and what the shader wants is `tint * sample` with the
    // coverage in ALPHA -- but the same multiplication has to serve a picture,
    // which carries its own colour. An R8 texture samples as (r, 0, 0, 1), which
    // is neither. Expanding to white-with-coverage-alpha makes one multiplication
    // correct for both, and the four megabytes buys not having a second pipeline,
    // a second sort and a state change per element.
    //
    // The CPU-side atlas stays one byte a texel, which is the honest storage;
    // this is the upload.
    staging_.resize(static_cast<core::usize>(source.width) * source.height * 4u);
    for (core::usize i = 0; i < source.pixels.size(); ++i) {
        staging_[i * 4 + 0] = std::byte{0xFF};
        staging_[i * 4 + 1] = std::byte{0xFF};
        staging_[i * 4 + 2] = std::byte{0xFF};
        staging_[i * 4 + 3] = std::byte{source.pixels[i]};
    }
    cmd.uploadTexture(atlas_, staging_, 0);
    uploadedVersion_ = source.version;
}

void UiText::destroy(rhi::IDevice& device)
{
    if (atlas_.valid()) {
        device.destroy(atlas_);
    }
    atlas_ = {};
    width_ = 0;
    height_ = 0;
    uploadedVersion_ = 0;
    staging_.clear();
    staging_.shrink_to_fit();
    setMounts(nullptr);
}

} // namespace luaug::app
