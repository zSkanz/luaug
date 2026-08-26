#include "luaug/app/thumbnails.h"

#include "luaug/asset/image.h"
#include "luaug/platform/async_io.h"
#include "luaug/rhi/descs.h"
#include "luaug/rhi/device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace luaug::app {
namespace {

using core::f32;
using core::f64;
using core::u32;
using core::u64;
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

ThumbnailCache::~ThumbnailCache()
{
    // No device, so no textures can be freed here -- `destroy` is what does
    // that, and a caller who forgot it has leaked them whatever this does. What
    // CANNOT be left is a job still writing into memory this object is about to
    // release, so that much is waited for unconditionally.
    for (Entry& entry : entries_) {
        if (entry.decode.valid())
            jobs::wait(entry.decode);
        if (entry.read.valid())
            platform::cancelIo(entry.read);
    }
}

ThumbnailCache::Entry* ThumbnailCache::find(std::string_view key) noexcept
{
    for (Entry& entry : entries_) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

usize ThumbnailCache::inFlight() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.stage == Stage::Reading || entry.stage == Stage::Decoding)
            ++count;
    }
    return count;
}

ThumbnailCache::Thumbnail ThumbnailCache::request(const std::filesystem::path& path)
{
    // Keyed by the path text rather than by the path, because the key is
    // compared far more often than it is made and comparing two paths is not a
    // string compare on every platform.
    const std::string key = path.string();

    if (Entry* found = find(key); found != nullptr) {
        found->lastWanted = frame_;
        if (found->stage != Stage::Ready || !found->texture.valid())
            return {};
        return Thumbnail{found->texture, found->width, found->height};
    }

    Entry made;
    made.key = key;
    made.lastWanted = frame_;
    entries_.push_back(std::move(made));
    return {};
}

void ThumbnailCache::admit()
{
    // **The most recently wanted first**, which is what makes this feel right
    // rather than merely bounded: what somebody is looking at now starts before
    // whatever they scrolled past on the way to it.
    while (inFlight() < MaxInFlight) {
        Entry* next = nullptr;
        for (Entry& entry : entries_) {
            if (entry.stage != Stage::Queued)
                continue;
            if (next == nullptr || entry.lastWanted > next->lastWanted)
                next = &entry;
        }
        if (next == nullptr)
            return;

        // `Low`, because a thumbnail is never what a frame is waiting for. A
        // streamed chunk the camera is about to reach is, and the two share this
        // queue.
        next->read = platform::readFileAsync(std::filesystem::path(next->key), platform::IoPriority::Low);
        if (!next->read.valid()) {
            // The service is not running, or its pool is full. Failing rather
            // than retrying: a browser open on a folder would otherwise ask
            // again every frame forever, and a row drawing its icon is a fine
            // answer.
            next->stage = Stage::Failed;
            continue;
        }
        next->stage = Stage::Reading;
    }
}

void ThumbnailCache::collectReads()
{
    for (Entry& entry : entries_) {
        if (entry.stage != Stage::Reading)
            continue;

        const platform::IoStatus status = platform::ioStatus(entry.read);
        if (status == platform::IoStatus::Pending)
            continue;

        auto work = std::make_unique<Work>();
        const bool got = status == platform::IoStatus::Ready && platform::takeIoResult(entry.read, work->bytes);
        // **A request that ended without being TAKEN still holds its
        // slot** -- `takeIoResult` releases one only for `Ready`, and the
        // pool is a fixed 512. A project with missing files would fill it
        // and every later read would be refused, which reads as "textures
        // stopped loading after a while" and has nothing in the log.
        // `cancelIo` is the documented way to let a terminal one go.
        if (!got)
            platform::cancelIo(entry.read);
        entry.read = {};
        if (!got || work->bytes.empty()) {
            entry.stage = Stage::Failed;
            continue;
        }

        // **The pointer the job writes through outlives `entries_` growing**,
        // which is why the work is on the heap: another `request` between now
        // and the job finishing would reallocate the vector, and a job holding
        // an entry's address would be writing into freed memory.
        Work* target = work.get();
        entry.work = std::move(work);
        // `noexcept`, which the pool requires and is right to: there is no
        // thread here to throw on. Nothing below can throw except an allocation,
        // and an allocation that fails in a thumbnail decoder is one the process
        // was not going to survive anyway.
        entry.decode = jobs::schedule("thumbnail-decode", jobs::Domain::AssetIo, [target]() noexcept {
            asset::Image decoded;
            if (asset::decodeImage(target->bytes, decoded).has_value())
                return;
            // Freed here rather than by the main thread: it is the biggest
            // allocation in the pipeline and nothing needs it again.
            target->bytes.clear();
            target->bytes.shrink_to_fit();
            target->ok = makeThumbnail(decoded, Edge, target->image);
        });

        if (!entry.decode.valid()) {
            entry.stage = Stage::Failed;
            entry.work.reset();
            continue;
        }
        entry.stage = Stage::Decoding;
    }
}

void ThumbnailCache::collectDecodes(rhi::IDevice& device, rhi::ICmdList& cmd)
{
    for (Entry& entry : entries_) {
        if (entry.stage != Stage::Decoding || !jobs::finished(entry.decode))
            continue;

        entry.decode = {};
        if (entry.work != nullptr && entry.work->ok) {
            // The one part that has to be here: an upload needs a command list,
            // and a command list belongs to a frame. It is also the cheap part
            // -- 64 KB, whatever the source was.
            entry.texture = upload(device, cmd, entry.work->image);
            entry.width = entry.work->image.width;
            entry.height = entry.work->image.height;
        }
        entry.stage = entry.texture.valid() ? Stage::Ready : Stage::Failed;
        entry.work.reset();
    }
}

void ThumbnailCache::evict(rhi::IDevice& device)
{
    if (entries_.size() <= Resident)
        return;

    // Least recently wanted goes -- but never one the pipeline is holding: its
    // job writes through a pointer this entry owns, and dropping the entry would
    // free that while the pool was still using it. An in-flight entry is by
    // definition one somebody just asked for anyway.
    std::stable_partition(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.stage == Stage::Reading || entry.stage == Stage::Decoding;
    });
    const usize held = static_cast<usize>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.stage == Stage::Reading || entry.stage == Stage::Decoding;
    }));
    if (entries_.size() <= std::max(Resident, held))
        return;

    std::sort(entries_.begin() + static_cast<std::ptrdiff_t>(held), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.lastWanted > b.lastWanted; });
    for (usize index = std::max(Resident, held); index < entries_.size(); ++index) {
        if (entries_[index].texture.valid())
            device.destroy(entries_[index].texture);
    }
    entries_.resize(std::max(Resident, held));
}

void ThumbnailCache::flush(rhi::IDevice& device, rhi::ICmdList& cmd)
{
    ++frame_;

    // Completions before admissions, so a read that finished during the frame
    // moves on in the same one rather than waiting for the next -- and so the
    // slot it frees is available to whatever is admitted below.
    // `StreamingHost::pump` orders its own pipeline the same way and says so.
    platform::pumpIo();
    collectReads();
    collectDecodes(device, cmd);
    admit();
    evict(device);
}

void ThumbnailCache::destroy(rhi::IDevice& device)
{
    for (Entry& entry : entries_) {
        // **Waited for, not abandoned.** The job writes into memory this object
        // owns, and returning while one is running is a use-after-free that
        // reproduces on a fast machine and never on a slow one.
        if (entry.decode.valid())
            jobs::wait(entry.decode);
        if (entry.read.valid())
            platform::cancelIo(entry.read);
        if (entry.texture.valid())
            device.destroy(entry.texture);
    }
    entries_.clear();
}

usize ThumbnailCache::residentCount() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.stage == Stage::Ready && entry.texture.valid())
            ++count;
    }
    return count;
}

usize ThumbnailCache::pendingCount() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.stage == Stage::Queued || entry.stage == Stage::Reading || entry.stage == Stage::Decoding)
            ++count;
    }
    return count;
}

} // namespace luaug::app
