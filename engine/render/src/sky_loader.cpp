// A `Sky`'s six images, off the frame thread (ADR 0096). See sky_loader.h.
#include "luaug/render/sky_loader.h"

#include "luaug/asset/content.h"
#include "luaug/asset/texture.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/platform/file.h"
#include "luaug/render/render_world.h"
#include "luaug/scene/world.h"

#include <array>
#include <chrono>
#include <string>
#include <string_view>

namespace luaug::render {
namespace {

using core::u32;

constexpr std::string_view kAssetScheme = "asset://";

// How many bands the octahedral picture is resampled in. Each is a job, so a
// machine with this many workers resamples the whole picture in the time one
// band takes; more bands than workers only queue.
constexpr u32 kBands = 8;

} // namespace

// Everything one bake owns, at a fixed address while its jobs run: they are
// handed pointers into it, and nothing here moves until the last has finished.
struct SkyLoader::Bake
{
    Key key;
    // What each face job reads: a file on disk, or bytes a mount already holds.
    struct Source
    {
        std::filesystem::path path;
        std::span<const std::byte> packed;
        std::string urn;
    };
    std::array<Source, kSkyFaceCount> sources;
    std::array<asset::Image, kSkyFaceCount> faces;
    std::array<bool, kSkyFaceCount> failed{};

    SkyTurn turn;
    std::vector<std::byte> picture;
    SkyRadiance radiance;

    std::array<jobs::JobHandle, kSkyFaceCount> reads{};
    std::array<jobs::JobHandle, kBands> bands{};
    jobs::JobHandle last{};
    std::chrono::steady_clock::time_point began;
    std::chrono::steady_clock::time_point ended;

    struct Band
    {
        Bake* bake = nullptr;
        u32 index = 0;
    };
    std::array<Band, kBands> bandArgs{};
    struct Face
    {
        Bake* bake = nullptr;
        u32 index = 0;
    };
    std::array<Face, kSkyFaceCount> faceArgs{};
};

SkyLoader::SkyLoader() = default;

SkyLoader::~SkyLoader()
{
    // A bake still running holds pointers into itself; it has to finish before
    // it can be let go.
    if (bake_ != nullptr)
        jobs::wait(bake_->last);
}

void SkyLoader::start(const scene::World& world, const Key& key)
{
    if (bake_ != nullptr)
        jobs::wait(bake_->last);
    bake_ = std::make_unique<Bake>();
    Bake& bake = *bake_;
    bake.key = key;
    bake.turn = skyTurnOf(key.orientation);
    bake.picture.assign(static_cast<std::size_t>(kSkyboxSize) * kSkyboxSize * 4, std::byte{0});
    bake.began = std::chrono::steady_clock::now();

    for (u32 face = 0; face < kSkyFaceCount; ++face) {
        Bake::Source& source = bake.sources[face];
        source.urn = std::string(world.atoms().text(key.faces[face]));
        if (source.urn.empty())
            continue;
        const asset::ResolvedContent resolved =
            mounts_ != nullptr ? mounts_->resolve(source.urn) : asset::ResolvedContent{};
        if (resolved.source == asset::ResolvedContent::Source::Pack && resolved.kind == asset::AssetKind::Texture) {
            source.packed = resolved.bytes;
        }
        else if (resolved.source == asset::ResolvedContent::Source::Loose) {
            source.path = resolved.path;
        }
        else {
            std::string_view relative = source.urn;
            if (relative.substr(0, kAssetScheme.size()) == kAssetScheme)
                relative.remove_prefix(kAssetScheme.size());
            source.path = contentRoot_ / std::filesystem::path(relative);
        }
    }

    // Read and decode each face: a packed one transcoded to plain eight-bit
    // texels, which is what the CPU can read, and a loose one decoded from its
    // file.
    for (u32 face = 0; face < kSkyFaceCount; ++face) {
        bake.faceArgs[face] = Bake::Face{&bake, face};
        bake.reads[face] = jobs::schedule(
            "sky-face", jobs::Domain::AssetIo,
            [](void* user) noexcept {
                auto* arg = static_cast<Bake::Face*>(user);
                Bake& owner = *arg->bake;
                const Bake::Source& source = owner.sources[arg->index];
                asset::Image& image = owner.faces[arg->index];
                if (source.urn.empty())
                    return;
                if (!source.packed.empty()) {
                    asset::TranscodeOptions options;
                    options.forceUncompressed = true;
                    options.baseLevelOnly = true;
                    asset::TextureAsset texture;
                    if (asset::transcodeTexture(source.packed, options, texture).has_value() || !texture.valid() ||
                        texture.format != asset::TextureFormat::Rgba8) {
                        owner.failed[arg->index] = true;
                        return;
                    }
                    const asset::TextureMip& level = texture.mips.front();
                    image.width = level.width;
                    image.height = level.height;
                    image.sourceChannels = 4;
                    image.pixels.assign(texture.pixels.begin() + static_cast<std::ptrdiff_t>(level.offset),
                                        texture.pixels.begin() +
                                            static_cast<std::ptrdiff_t>(level.offset + level.size));
                    return;
                }
                std::vector<std::byte> bytes;
                if (!platform::readFile(source.path, bytes) || asset::decodeImage(bytes, image).has_value())
                    owner.failed[arg->index] = true;
            },
            &bake.faceArgs[face]);
    }

    // The bands, once every face is in.
    for (u32 band = 0; band < kBands; ++band) {
        bake.bandArgs[band] = Bake::Band{&bake, band};
        bake.bands[band] = jobs::schedule(
            "sky-resample", jobs::Domain::AssetIo,
            [](void* user) noexcept {
                auto* arg = static_cast<Bake::Band*>(user);
                Bake& owner = *arg->bake;
                std::array<const asset::Image*, kSkyFaceCount> faces{};
                for (u32 face = 0; face < kSkyFaceCount; ++face)
                    faces[face] = owner.faces[face].valid() ? &owner.faces[face] : nullptr;
                const u32 rows = kSkyboxSize / kBands;
                resampleSkybox(faces, owner.turn, kSkyboxSize, arg->index * rows, (arg->index + 1) * rows,
                               owner.picture);
            },
            &bake.bandArgs[band], bake.reads);
    }

    // And the linear copy the environment reads, once every band is done.
    bake.last = jobs::schedule(
        "sky-radiance", jobs::Domain::AssetIo,
        [](void* user) noexcept {
            Bake& owner = *static_cast<Bake*>(user);
            skyRadianceOf(owner.picture, kSkyboxSize, kSkyRadianceSize, owner.radiance);
            owner.ended = std::chrono::steady_clock::now();
        },
        &bake, bake.bands);
}

void SkyLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, const RenderSky& look)
{
    Key key;
    const bool any = look.present && look.hasImages();
    if (any) {
        for (u32 face = 0; face < kSkyFaceCount; ++face)
            key.faces[face] = look.faces[face];
        key.orientation = look.orientation;
    }

    // Nothing asked for: the picture is dropped, and the analytic sky draws.
    if (!any) {
        wantedAny_ = false;
        return;
    }
    if (!wantedAny_ || !(key == wanted_)) {
        wanted_ = key;
        wantedAny_ = true;
        start(world, key);
    }

    if (bake_ == nullptr)
        return;
    if (synchronous_)
        jobs::wait(bake_->last);
    else if (!jobs::finished(bake_->last))
        return;

    Bake& bake = *bake_;
    for (u32 face = 0; face < kSkyFaceCount; ++face) {
        if (bake.failed[face]) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"path", bake.sources[face].urn}};
            core::log(core::LogLevel::Warn, LUAUG_TR("render.err.sky_face_missing"), args);
        }
    }

    const rhi::TextureHandle next = device.createTexture({
        .format = rhi::TextureFormat::Rgba8UnormSrgb,
        .usage = rhi::TextureUsage::Sampled,
        .width = kSkyboxSize,
        .height = kSkyboxSize,
        .debugName = "sky",
    });
    if (next.valid()) {
        cmd.uploadTexture(next, bake.picture, 0);
        if (texture_.valid())
            device.destroy(texture_);
        texture_ = next;
        radiance_ = std::make_shared<const SkyRadiance>(std::move(bake.radiance));
    }
    lastBakeMs_ = std::chrono::duration<core::f64, std::milli>(bake.ended - bake.began).count();
    // Said once per bake, because its cost is ADR 0096's `Sky` budget and this
    // is where it is measured.
    const std::array<core::I18nArg, 1> took{core::I18nArg{"ms", lastBakeMs_}};
    core::log(core::LogLevel::Info, LUAUG_TR("render.info.sky_baked"), took);
    bake_.reset();
}

void SkyLoader::append(RenderWorld& out) const
{
    if (!out.look.sky.present || !out.look.sky.hasImages() || !texture_.valid())
        return;
    out.look.sky.image = texture_;
    out.look.sky.radiance = radiance_;
}

void SkyLoader::destroy(rhi::IDevice& device)
{
    if (bake_ != nullptr) {
        jobs::wait(bake_->last);
        bake_.reset();
    }
    if (texture_.valid())
        device.destroy(texture_);
    texture_ = {};
    radiance_.reset();
    wantedAny_ = false;
}

} // namespace luaug::render
