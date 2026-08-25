#include "luaug/app/thumbnails.h"

#include "luaug/asset/image.h"
#include "luaug/platform/file.h"
#include "luaug/rhi/descs.h"
#include "luaug/rhi/device.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace luaug::app {
namespace {

using core::f32;
using core::u32;
using core::u8;
using core::usize;

// sRGB byte -> linear, once, at first use. 256 entries against a `std::pow` per
// texel per channel: a 4K source is 33 million of them, which is the difference
// between a thumbnail costing milliseconds and costing the best part of a
// second.
const std::array<f32, 256>& toLinearTable()
{
    static const std::array<f32, 256> table = [] {
        std::array<f32, 256> made{};
        for (usize i = 0; i < made.size(); ++i) {
            const f32 c = static_cast<f32>(i) / 255.0f;
            made[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return made;
    }();
    return table;
}

[[nodiscard]] u8 toSrgbByte(f32 linear) noexcept
{
    const f32 clamped = std::clamp(linear, 0.0f, 1.0f);
    const f32 encoded = clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

[[nodiscard]] rhi::TextureHandle upload(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Image& image)
{
    const rhi::TextureHandle handle = device.createTexture({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .usage = rhi::TextureUsage::Sampled,
        .width = image.width,
        .height = image.height,
        .debugName = "content-thumbnail",
    });
    if (!handle.valid())
        return {};
    cmd.uploadTexture(handle, image.pixels, 0);
    return handle;
}

} // namespace

bool makeThumbnail(const asset::Image& source, u32 edge, asset::Image& out)
{
    if (!source.valid() || edge == 0)
        return false;

    // The longer side lands on `edge`; the shorter one keeps the ratio and never
    // rounds away to nothing -- a 4096x3 strip is a real file, and a zero-height
    // texture is not a texture.
    const u32 longer = std::max(source.width, source.height);
    const u32 width = std::max(1u, source.width * edge / longer);
    const u32 height = std::max(1u, source.height * edge / longer);

    out = asset::Image{};
    out.width = width;
    out.height = height;
    out.sourceChannels = source.sourceChannels;
    out.pixels.assign(static_cast<usize>(width) * height * 4u, std::byte{0});

    const std::array<f32, 256>& toLinear = toLinearTable();
    const auto* pixels = reinterpret_cast<const u8*>(source.pixels.data());
    auto* target = reinterpret_cast<u8*>(out.pixels.data());

    for (u32 y = 0; y < height; ++y) {
        const u32 y0 = y * source.height / height;
        const u32 y1 = std::max(y0 + 1u, (y + 1u) * source.height / height);
        for (u32 x = 0; x < width; ++x) {
            const u32 x0 = x * source.width / width;
            const u32 x1 = std::max(x0 + 1u, (x + 1u) * source.width / width);

            // RGB accumulates PREMULTIPLIED and is divided by the alpha sum;
            // alpha accumulates alone and is divided by the count. That is what
            // keeps a cut-out edge the colour of the cut-out rather than the
            // colour of the nothing beside it.
            f32 r = 0.0f;
            f32 g = 0.0f;
            f32 b = 0.0f;
            f32 a = 0.0f;
            u32 count = 0;
            for (u32 sy = y0; sy < y1; ++sy) {
                for (u32 sx = x0; sx < x1; ++sx) {
                    const usize index = (static_cast<usize>(sy) * source.width + sx) * 4u;
                    const f32 alpha = static_cast<f32>(pixels[index + 3u]) / 255.0f;
                    r += toLinear[pixels[index + 0u]] * alpha;
                    g += toLinear[pixels[index + 1u]] * alpha;
                    b += toLinear[pixels[index + 2u]] * alpha;
                    a += alpha;
                    ++count;
                }
            }

            const usize index = (static_cast<usize>(y) * width + x) * 4u;
            // Fully transparent in, fully transparent out, and no colour to
            // recover: a zero alpha sum is the one case the weighting cannot
            // answer, and black is what a texel nobody can see looks like.
            const f32 weight = a > 0.0f ? 1.0f / a : 0.0f;
            target[index + 0u] = toSrgbByte(r * weight);
            target[index + 1u] = toSrgbByte(g * weight);
            target[index + 2u] = toSrgbByte(b * weight);
            target[index + 3u] =
                count > 0 ? static_cast<u8>(std::lround(std::clamp(a / static_cast<f32>(count), 0.0f, 1.0f) * 255.0f))
                          : u8{0};
        }
    }
    return true;
}

ThumbnailCache::Entry* ThumbnailCache::find(std::string_view key) noexcept
{
    for (Entry& entry : entries_) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

ThumbnailCache::Thumbnail ThumbnailCache::request(const std::filesystem::path& path)
{
    // Keyed by the path text rather than by the path, because the key is
    // compared far more often than it is made and comparing two paths is not a
    // string compare on every platform.
    const std::string key = path.string();

    if (Entry* found = find(key); found != nullptr) {
        found->lastWanted = frame_;
        if (found->failed || found->pending || !found->texture.valid())
            return {};
        return Thumbnail{found->texture, found->width, found->height};
    }

    Entry made;
    made.key = key;
    made.lastWanted = frame_;
    entries_.push_back(std::move(made));
    return {};
}

void ThumbnailCache::flush(rhi::IDevice& device, rhi::ICmdList& cmd)
{
    ++frame_;

    // **The most recently wanted first**, which is what makes this feel right
    // rather than merely bounded: what somebody is looking at now fills in
    // before whatever they scrolled past on the way to it.
    usize done = 0;
    while (done < PerFrame) {
        Entry* next = nullptr;
        for (Entry& entry : entries_) {
            if (!entry.pending)
                continue;
            if (next == nullptr || entry.lastWanted > next->lastWanted)
                next = &entry;
        }
        if (next == nullptr)
            break;

        // Marked done before anything can go wrong, so a file that cannot be
        // read costs one attempt and not one per frame.
        next->pending = false;
        next->failed = true;
        ++done;

        std::vector<std::byte> bytes;
        asset::Image decoded;
        if (!platform::readFile(std::filesystem::path(next->key), bytes) ||
            asset::decodeImage(bytes, decoded).has_value()) {
            // Not logged. A folder can hold a `.png` that is a rename of
            // something else, and a warning per such file per session is noise
            // about a row that draws its icon and is otherwise fine.
            continue;
        }

        asset::Image small;
        if (!makeThumbnail(decoded, Edge, small))
            continue;

        next->texture = upload(device, cmd, small);
        next->width = small.width;
        next->height = small.height;
        next->failed = !next->texture.valid();
    }

    if (entries_.size() <= Resident)
        return;

    // Least recently wanted goes. Sorted rather than partially selected because
    // the list is a few hundred at most, and this runs on the frames somebody is
    // scrolling -- where being obviously correct is worth more than the
    // microsecond.
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.lastWanted > b.lastWanted; });
    for (usize index = Resident; index < entries_.size(); ++index) {
        if (entries_[index].texture.valid())
            device.destroy(entries_[index].texture);
    }
    entries_.resize(Resident);
}

void ThumbnailCache::destroy(rhi::IDevice& device)
{
    for (Entry& entry : entries_) {
        if (entry.texture.valid())
            device.destroy(entry.texture);
    }
    entries_.clear();
}

usize ThumbnailCache::residentCount() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.texture.valid())
            ++count;
    }
    return count;
}

usize ThumbnailCache::pendingCount() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.pending)
            ++count;
    }
    return count;
}

} // namespace luaug::app
