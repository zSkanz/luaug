#include "luaug/app/thumbnails.h"

#include "luaug/app/content_tree.h"
#include "luaug/asset/gltf.h"
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
    // **Asked up front rather than discovered after a read** (S5.16), which is
    // what the kind is for: a folder, a sound or a font is never opened, and a
    // mesh is not handed to an image decoder to find out it is not a picture.
    made.kind = previewKindOf(path);
    if (made.kind == PreviewKind::None || (made.kind != PreviewKind::Texture && previews_ == nullptr))
        made.stage = Stage::Failed;
    entries_.push_back(std::move(made));
    return {};
}

void ThumbnailCache::setPreviewRenderer(IPreviewRenderer* renderer) noexcept
{
    const bool gained = previews_ == nullptr && renderer != nullptr;
    previews_ = renderer;
    if (!gained)
        return;

    // **Forgets what was refused for want of a renderer.** Without this, the
    // order of two lines in the host's startup would decide whether a whole
    // folder ever gets pictures -- the browser draws a frame, every mesh row is
    // remembered as Failed, and a renderer arriving a moment later changes
    // nothing. Only the refusals: a file that is genuinely not a picture stays
    // failed.
    for (Entry& entry : entries_) {
        if (entry.stage == Stage::Failed && entry.kind != PreviewKind::None && entry.kind != PreviewKind::Texture &&
            !entry.texture.valid()) {
            entry.stage = Stage::Queued;
        }
    }
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
        if (entry.kind == PreviewKind::Subtree) {
            // Nothing to parse off the frame: the TEXT is the payload, and
            // building a tree from it needs a world the render half has and this
            // does not. Straight to the frame that has a command list.
            target->ok = true;
            entry.stage = Stage::Drawing;
            continue;
        }

        // Where a glTF resolves its external buffers and images from. Set here
        // rather than captured, because the job body has to stay copyable.
        target->baseDirectory = std::filesystem::path(entry.key).parent_path();

        const PreviewKind kind = entry.kind;
        entry.decode = jobs::schedule("thumbnail-decode", jobs::Domain::AssetIo, [target, kind]() noexcept {
            if (kind == PreviewKind::Mesh) {
                // **Parsed here and not on the frame**, which is the whole
                // reason this stage exists for a mesh: a 3 MB glTF parsed on the
                // frame thread is D118, the defect that made the editor feel
                // like it reloaded the world whenever anybody touched anything.
                // Default options: a preview wants what the file says, and any
                // switch turned on here would make the picture disagree with
                // the thing the viewport draws.
                const asset::GltfImportOptions options;
                if (asset::importGltf(target->bytes, target->baseDirectory, options, target->model).has_value())
                    return;
                target->bytes.clear();
                target->bytes.shrink_to_fit();
                target->ok = true;
                return;
            }

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
        if (entry.kind == PreviewKind::Mesh) {
            entry.stage = (entry.work != nullptr && entry.work->ok) ? Stage::Drawing : Stage::Failed;
            if (entry.stage == Stage::Failed)
                entry.work.reset();
            continue;
        }
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

void ThumbnailCache::collectDraws(rhi::IDevice& device, rhi::ICmdList& cmd)
{
    if (previews_ == nullptr)
        return;

    // **Budgeted, because a preview is an upload plus a pass and neither can be
    // split.** One a frame: a folder of forty models fills in over forty frames
    // rather than stopping the editor for one long one, which is the same
    // trade every other stage in this pipeline makes.
    usize drawn = 0;
    while (drawn < MaxPreviewsPerFrame) {
        Entry* next = nullptr;
        for (Entry& entry : entries_) {
            if (entry.stage != Stage::Drawing)
                continue;
            // Most recently wanted first, as `admit` does: what somebody is
            // looking at now is drawn before what they scrolled past.
            if (next == nullptr || entry.lastWanted > next->lastWanted)
                next = &entry;
        }
        if (next == nullptr)
            return;

        PreviewJob job;
        job.kind = next->kind;
        job.path = std::filesystem::path(next->key);
        job.edge = Edge;
        if (next->work != nullptr) {
            if (job.kind == PreviewKind::Mesh)
                job.model = &next->work->model;
            else if (job.kind == PreviewKind::Subtree) {
                job.text =
                    std::string_view(reinterpret_cast<const char*>(next->work->bytes.data()), next->work->bytes.size());
            }
        }

        PreviewResult result;
        const bool ok = previews_->drawPreview(device, cmd, job, result) && result.texture.valid();
        if (ok) {
            next->texture = result.texture;
            next->width = result.width;
            next->height = result.height;
        }
        next->stage = ok ? Stage::Ready : Stage::Failed;
        // **Freed the moment the call returns**, which is the contract: a parsed
        // glTF is the largest thing in this pipeline and nothing needs it twice.
        next->work.reset();
        ++drawn;
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
    collectDraws(device, cmd);
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

// --- What a row is a picture of, and where it is drawn from (S5.16) ----------

PreviewKind previewKindOf(const std::filesystem::path& path) noexcept
{
    // Delegated rather than re-decided, which is the header's own requirement:
    // the browser and this cache cannot come to disagree about what a `.gltf`
    // is if only one of them answers that question.
    // The FILE NAME, because that is what the extension rule reads and a full
    // path would make `.scene.json` inside a folder called `x.gltf` answer wrong.
    const std::string name = path.filename().string();
    switch (contentKindOf(name)) {
    case ContentKind::Texture:
        return PreviewKind::Texture;
    case ContentKind::Mesh:
        return PreviewKind::Mesh;
    case ContentKind::Scene:
    case ContentKind::Stamp:
        // Two kinds to the browser -- one is opened and the other placed -- and
        // one here, because both are drawn by pointing a camera at a subtree.
        return PreviewKind::Subtree;
    default:
        // A folder, a sound, a font, a chunk, a file this build does not know.
        // The row wears its class icon and this cache never opens it.
        return PreviewKind::None;
    }
}

render::ViewOverride previewView(const core::AABB& bounds) noexcept
{
    render::ViewOverride view;

    // **An empty box is framed as a unit box at the origin.** `center` and
    // `size` of an empty `AABB` are built from infinities and produce NaN, and a
    // camera full of NaN is not recoverable -- a preview of nothing should be a
    // picture of nothing rather than a crash or a black square nobody can
    // explain.
    core::Vec3 centre{0.0f, 0.0f, 0.0f};
    f32 radius = 0.8660254f; // half the diagonal of a unit cube
    if (bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z) {
        centre = core::Vec3{(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                            (bounds.min.z + bounds.max.z) * 0.5f};
        const core::Vec3 extent{(bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.y - bounds.min.y) * 0.5f,
                                (bounds.max.z - bounds.min.z) * 0.5f};
        // **The bounding SPHERE and not the box.** A sphere subtends the same
        // angle from every direction, so the distance cannot depend on which way
        // the fixed view happens to point and a model cannot fall out of frame
        // because it is long along the axis the camera looks down. It costs a
        // little empty space around a flat asset, which is the right way to be
        // wrong.
        radius = core::length(extent);
        if (!(radius > 1.0e-4f))
            radius = 1.0e-4f;
    }

    // **Three-quarter: above, in front, and to one side.** The convention rather
    // than a preference -- a straight-on view of a cube is a square, and every
    // asset browser worth using draws models this way so a box reads as a box
    // and a character reads as facing somewhere.
    const core::Vec3 direction = core::normalize(core::Vec3{-0.55f, -0.42f, -0.72f});

    // A square target, so the vertical and horizontal fields of view are equal
    // and one distance frames both. The half-angle is what the sphere has to fit
    // inside; the margin keeps the silhouette off the edge of the tile.
    constexpr f32 kFovDegrees = 35.0f;
    constexpr f32 kMargin = 1.15f;
    const f32 halfAngle = (kFovDegrees * 0.5f) * (3.14159265358979323846f / 180.0f);
    const f32 distance = (radius * kMargin) / std::sin(halfAngle);

    const core::Vec3 eye{centre.x - direction.x * distance, centre.y - direction.y * distance,
                         centre.z - direction.z * distance};

    // Widened explicitly. `DVec3` is f64 and these are f32, and an implicit
    // promotion is `-Wdouble-promotion` on Clang -- which MSVC does not raise,
    // so the Tier-2 stage is the only thing that would have caught it.
    view.cframe = core::lookAtCFrame(
        core::DVec3{static_cast<core::f64>(eye.x), static_cast<core::f64>(eye.y), static_cast<core::f64>(eye.z)},
        core::DVec3{static_cast<core::f64>(centre.x), static_cast<core::f64>(centre.y),
                    static_cast<core::f64>(centre.z)},
        core::Vec3{0.0f, 1.0f, 0.0f});
    view.fieldOfView = kFovDegrees;
    // **Scaled to the asset rather than fixed**, so a preview of a one-metre
    // crate and one of a two-hundred-metre terrain both have depth precision
    // where the geometry is. A fixed 0.1-to-5000 range spends almost all of its
    // precision on empty space for the first and runs out for the second.
    view.nearPlane = std::max(0.01f, distance - radius * 2.0f);
    view.farPlane = distance + radius * 4.0f;
    return view;
}

} // namespace luaug::app
