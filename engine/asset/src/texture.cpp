#include "luaug/asset/texture.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <basisu_transcoder.h>
#include <mutex>

namespace luaug::asset {
namespace {

using core::I18nArg;

// Upstream builds several codebook tables on first use and documents the call
// as one-per-process. `call_once` rather than a bare bool because a transcode
// may be issued from a job (`Domain::AssetIo`) and two workers arriving at once
// would race those tables.
void ensureTranscoderReady()
{
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

[[nodiscard]] u32 blocksAcross(u32 pixels) noexcept
{
    return (pixels + 3u) / 4u;
}

// The bytes one mip level occupies once transcoded. Written out rather than
// asked of the transcoder, because it is also what sizes the buffer handed IN.
[[nodiscard]] usize levelBytes(TextureFormat format, u32 width, u32 height) noexcept
{
    if (!isBlockCompressed(format)) {
        return static_cast<usize>(width) * height * 4u;
    }
    const usize blocks = static_cast<usize>(blocksAcross(width)) * blocksAcross(height);
    // BC1 is eight bytes a block; BC3, BC5 and BC7 are sixteen.
    return format == TextureFormat::Bc1Rgb ? blocks * 8u : blocks * 16u;
}

[[nodiscard]] basist::transcoder_texture_format toBasis(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Bc1Rgb:
        return basist::transcoder_texture_format::cTFBC1_RGB;
    case TextureFormat::Bc3Rgba:
        return basist::transcoder_texture_format::cTFBC3_RGBA;
    case TextureFormat::Bc5Rg:
        return basist::transcoder_texture_format::cTFBC5_RG;
    case TextureFormat::Bc7Rgba:
        return basist::transcoder_texture_format::cTFBC7_RGBA;
    case TextureFormat::Rgba8:
    case TextureFormat::Unknown:
        break;
    }
    return basist::transcoder_texture_format::cTFRGBA32;
}

// The best format the caller can sample, given what the texture holds.
//
// BC7 first because it is the only block format that is good at both colour
// and alpha; BC1 for opaque when BC7 is unavailable; BC3 for alpha when BC7 is
// unavailable. RGBA when nothing else is allowed -- which is the headless and
// capture path, and is also what makes a golden comparable across machines.
[[nodiscard]] TextureFormat chooseFormat(const TranscodeOptions& options, bool hasAlpha) noexcept
{
    if (options.forceUncompressed) {
        return TextureFormat::Rgba8;
    }
    if (options.allowBc7) {
        return TextureFormat::Bc7Rgba;
    }
    if (hasAlpha) {
        return options.allowBc3 ? TextureFormat::Bc3Rgba : TextureFormat::Rgba8;
    }
    return options.allowBc1 ? TextureFormat::Bc1Rgb : TextureFormat::Rgba8;
}

} // namespace

const char* textureFormatName(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Rgba8:
        return "rgba8";
    case TextureFormat::Bc1Rgb:
        return "bc1";
    case TextureFormat::Bc3Rgba:
        return "bc3";
    case TextureFormat::Bc5Rg:
        return "bc5";
    case TextureFormat::Bc7Rgba:
        return "bc7";
    case TextureFormat::Unknown:
        break;
    }
    return "unknown";
}

bool isBlockCompressed(TextureFormat format) noexcept
{
    return format == TextureFormat::Bc1Rgb || format == TextureFormat::Bc3Rgba || format == TextureFormat::Bc5Rg ||
           format == TextureFormat::Bc7Rgba;
}

std::optional<core::EngineError> transcodeTexture(std::span<const std::byte> ktx2, const TranscodeOptions& options,
                                                  TextureAsset& out)
{
    out = TextureAsset{};
    ensureTranscoderReady();

    if (ktx2.empty()) {
        return core::makeError(LUAUG_TR("asset.texture.err.malformed"));
    }

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(ktx2.data(), static_cast<u32>(ktx2.size()))) {
        return core::makeError(LUAUG_TR("asset.texture.err.malformed"));
    }
    if (transcoder.is_hdr()) {
        // The engine has no HDR texture path. Refused by name rather than
        // transcoded into something that looks washed out.
        return core::makeError(LUAUG_TR("asset.texture.err.hdr_unsupported"));
    }
    if (!transcoder.start_transcoding()) {
        return core::makeError(LUAUG_TR("asset.texture.err.transcode_failed"));
    }

    out.width = transcoder.get_width();
    out.height = transcoder.get_height();
    out.hasAlpha = transcoder.get_has_alpha() != 0;
    out.srgb = transcoder.is_srgb();
    out.format = chooseFormat(options, out.hasAlpha);

    if (out.width == 0 || out.height == 0) {
        return core::makeError(LUAUG_TR("asset.texture.err.malformed"));
    }

    const u32 declaredLevels = transcoder.get_levels();
    const u32 levels = options.baseLevelOnly ? 1u : (declaredLevels > 0 ? declaredLevels : 1u);
    const basist::transcoder_texture_format target = toBasis(out.format);
    const bool blocks = isBlockCompressed(out.format);

    usize total = 0;
    out.mips.reserve(levels);
    for (u32 level = 0; level < levels; ++level) {
        basist::ktx2_image_level_info info{};
        if (!transcoder.get_image_level_info(info, level, 0, 0)) {
            return core::makeError(LUAUG_TR("asset.texture.err.malformed"));
        }
        TextureMip mip;
        mip.width = info.m_orig_width;
        mip.height = info.m_orig_height;
        mip.offset = total;
        mip.size = levelBytes(out.format, mip.width, mip.height);
        total += mip.size;
        out.mips.push_back(mip);
    }

    out.pixels.resize(total);
    for (u32 level = 0; level < levels; ++level) {
        const TextureMip& mip = out.mips[level];
        // The unit of `output_blocks_buf_size_in_blocks_or_pixels` is what its
        // name says and it differs by format, which is the one thing easy to
        // get wrong here: blocks for a block format, PIXELS for RGBA.
        const u32 capacity = blocks ? blocksAcross(mip.width) * blocksAcross(mip.height) : mip.width * mip.height;
        if (!transcoder.transcode_image_level(level, 0, 0, out.pixels.data() + mip.offset, capacity, target)) {
            const I18nArg args[] = {{"format", textureFormatName(out.format)}};
            return core::makeError(LUAUG_TR("asset.texture.err.transcode_failed"), args);
        }
    }

    return std::nullopt;
}

} // namespace luaug::asset
