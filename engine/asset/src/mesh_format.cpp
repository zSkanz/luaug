#include "luaug/asset/mesh_format.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <meshoptimizer.h>
#include <string>

namespace luaug::asset {
namespace {

using core::AABB;
using core::CFrameD;
using core::f32;
using core::I18nArg;
using core::Mat4;
using core::Vec3;

// Pinned rather than inherited. Both encoders read a process-global version
// that upstream documents as not thread-safe, and a format whose bytes depend
// on whatever the last caller set is not a format (`meshoptimizer.h:274-280`,
// `:380-385`).
constexpr int VertexEncodeVersion = 1;
constexpr int VertexEncodeLevel = 2;
constexpr int IndexEncodeVersion = 1;

// magic(4) + version(4) + flags(4) + sectionCount(4) + bounds(24). The section
// table follows immediately.
constexpr usize HeaderBytes = 40;
constexpr usize SectionBytes = 24;

// Fixed record sizes, so a section's element count can be checked against its
// LENGTH rather than believed. Every one of these is exact arithmetic on what
// the writer below emits; the two compressed streams -- vertices and skin --
// are the only ones that cannot be checked this way, and `MaxMeshVertices` is
// what stands in for it there.
constexpr u64 LodRecordBytes = 32;
constexpr u64 SubmeshRecordBytes = 36;
constexpr u64 MaterialRecordBytes = 92;
constexpr u64 ImageRecordBytes = 24;
constexpr u64 JointRecordBytes = 132;
constexpr u64 ClipRecordBytes = 16;
constexpr u64 ChannelRecordBytes = 28;
constexpr u64 MeshletRecordBytes = 48;

// Four-byte tags, compared as bytes. Sorted in the file, so a reader may binary
// search and a writer cannot produce two files that differ only in section
// order.
constexpr u32 tag(const char (&text)[5])
{
    return static_cast<u32>(static_cast<unsigned char>(text[0])) |
           (static_cast<u32>(static_cast<unsigned char>(text[1])) << 8) |
           (static_cast<u32>(static_cast<unsigned char>(text[2])) << 16) |
           (static_cast<u32>(static_cast<unsigned char>(text[3])) << 24);
}

constexpr u32 TagVertices = tag("VTXS");
constexpr u32 TagSkin = tag("SKIN");
constexpr u32 TagLods = tag("LODS");
constexpr u32 TagIndexData = tag("IDXD");
constexpr u32 TagSubmeshes = tag("SUBM");
constexpr u32 TagMaterials = tag("MTLS");
constexpr u32 TagImages = tag("IMGS");
constexpr u32 TagJoints = tag("JNTS");
constexpr u32 TagClips = tag("CLPS");
constexpr u32 TagChannels = tag("CHNS");
constexpr u32 TagAnimationFloats = tag("ANMF");
constexpr u32 TagMeshlets = tag("MSHL");
constexpr u32 TagMeshletVertices = tag("MLTV");
constexpr u32 TagMeshletTriangles = tag("MLTT");
constexpr u32 TagStrings = tag("STRS");

// --- writing ---------------------------------------------------------------

class Writer
{
public:
    void u32v(u32 value)
    {
        for (usize i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
        }
    }

    void u64v(u64 value)
    {
        for (usize i = 0; i < 8; ++i) {
            m_bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
        }
    }

    // Bit-exact: the float goes in as the bits it already is, so an encode is
    // not a rounding. A format that re-parsed a decimal here would make two
    // builds of one mesh differ in the last ulp.
    void f32v(f32 value)
    {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32v(bits);
    }

    void vec3(const Vec3& value)
    {
        f32v(value.x);
        f32v(value.y);
        f32v(value.z);
    }

    void aabb(const AABB& value)
    {
        vec3(value.min);
        vec3(value.max);
    }

    void mat4(const Mat4& value)
    {
        for (usize row = 0; row < 4; ++row) {
            for (usize column = 0; column < 4; ++column) {
                f32v(value.m[row][column]);
            }
        }
    }

    void cframe(const CFrameD& value)
    {
        u64 bits = 0;
        for (const core::f64 component : {value.position.x, value.position.y, value.position.z}) {
            std::memcpy(&bits, &component, sizeof(bits));
            u64v(bits);
        }
        for (usize row = 0; row < 3; ++row) {
            for (usize column = 0; column < 3; ++column) {
                f32v(value.rotation.m[row][column]);
            }
        }
    }

    void raw(std::span<const std::byte> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }

    void pad4()
    {
        while ((m_bytes.size() % 4) != 0) {
            m_bytes.push_back(std::byte{0});
        }
    }

    [[nodiscard]] usize size() const noexcept { return m_bytes.size(); }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return m_bytes; }

private:
    std::vector<std::byte> m_bytes;
};

// Deduplicating string pool. Dedup matters for determinism as much as for size:
// the pool is written in insertion order, and insertion order is fixed by the
// order the encoder walks its own data.
class StringPool
{
public:
    u32 add(const std::string& text)
    {
        const auto existing = m_offsets.find(text);
        if (existing != m_offsets.end()) {
            return existing->second;
        }
        const auto offset = static_cast<u32>(m_bytes.size());
        m_bytes.insert(m_bytes.end(), reinterpret_cast<const std::byte*>(text.data()),
                       reinterpret_cast<const std::byte*>(text.data()) + text.size());
        m_bytes.push_back(std::byte{0});
        m_offsets.emplace(text, offset);
        return offset;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_bytes; }

private:
    std::vector<std::byte> m_bytes;
    std::map<std::string, u32> m_offsets;
};

[[nodiscard]] std::vector<std::byte> encodeVertexStream(const void* data, usize count, usize stride)
{
    if (count == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(meshopt_encodeVertexBufferBound(count, stride));
    const usize written = meshopt_encodeVertexBufferLevel(buffer.data(), buffer.size(), data, count, stride,
                                                          VertexEncodeLevel, VertexEncodeVersion);
    std::vector<std::byte> out(written);
    std::memcpy(out.data(), buffer.data(), written);
    return out;
}

[[nodiscard]] std::vector<std::byte> encodeIndexStream(std::span<const u32> indices, usize vertexCount)
{
    if (indices.empty()) {
        return {};
    }
    meshopt_encodeIndexVersion(IndexEncodeVersion);
    std::vector<unsigned char> buffer(meshopt_encodeIndexBufferBound(indices.size(), vertexCount));
    const usize written = meshopt_encodeIndexBuffer(buffer.data(), buffer.size(), indices.data(), indices.size());
    std::vector<std::byte> out(written);
    std::memcpy(out.data(), buffer.data(), written);
    return out;
}

// --- reading ---------------------------------------------------------------

class Reader
{
public:
    Reader(std::span<const std::byte> bytes, usize offset) : m_bytes(bytes), m_at(offset) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] usize at() const noexcept { return m_at; }

    [[nodiscard]] bool has(usize count) const noexcept { return m_ok && m_bytes.size() - m_at >= count; }

    u32 u32v()
    {
        if (!has(4)) {
            m_ok = false;
            return 0;
        }
        u32 value = 0;
        for (usize i = 0; i < 4; ++i) {
            value |= static_cast<u32>(static_cast<unsigned char>(m_bytes[m_at + i])) << (i * 8);
        }
        m_at += 4;
        return value;
    }

    u64 u64v()
    {
        if (!has(8)) {
            m_ok = false;
            return 0;
        }
        u64 value = 0;
        for (usize i = 0; i < 8; ++i) {
            value |= static_cast<u64>(static_cast<unsigned char>(m_bytes[m_at + i])) << (i * 8);
        }
        m_at += 8;
        return value;
    }

    f32 f32v()
    {
        const u32 bits = u32v();
        f32 value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    Vec3 vec3()
    {
        Vec3 value;
        value.x = f32v();
        value.y = f32v();
        value.z = f32v();
        return value;
    }

    AABB aabb()
    {
        AABB value;
        value.min = vec3();
        value.max = vec3();
        return value;
    }

    Mat4 mat4()
    {
        Mat4 value;
        for (usize row = 0; row < 4; ++row) {
            for (usize column = 0; column < 4; ++column) {
                value.m[row][column] = f32v();
            }
        }
        return value;
    }

    CFrameD cframe()
    {
        CFrameD value;
        core::f64 components[3] = {0.0, 0.0, 0.0};
        for (core::f64& component : components) {
            const u64 bits = u64v();
            std::memcpy(&component, &bits, sizeof(component));
        }
        value.position = {components[0], components[1], components[2]};
        for (usize row = 0; row < 3; ++row) {
            for (usize column = 0; column < 3; ++column) {
                value.rotation.m[row][column] = f32v();
            }
        }
        return value;
    }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
    bool m_ok = true;
};

struct Section
{
    u32 tag = 0;
    u64 offset = 0;
    u64 length = 0;
    u32 count = 0;
};

[[nodiscard]] const Section* findSection(const std::vector<Section>& sections, u32 wanted)
{
    for (const Section& section : sections) {
        if (section.tag == wanted) {
            return &section;
        }
    }
    return nullptr;
}

// A section's declared element count has to fit inside the bytes the section
// actually has. Checked before a single `resize` or `reserve`, because a
// corrupted count reaching an allocator is a `bad_alloc` rather than an error
// message -- which is precisely what the first run of the corruption case
// produced.
[[nodiscard]] bool countFits(const Section& section, u64 recordBytes)
{
    return static_cast<u64>(section.count) <= section.length / recordBytes;
}

[[nodiscard]] core::EngineError malformed()
{
    return core::makeError(LUAUG_TR("asset.mesh.err.malformed"));
}

// A nul-terminated string out of the pool, refusing anything that runs off the
// end -- which is what a corrupted offset looks like.
[[nodiscard]] bool poolString(std::span<const std::byte> pool, u32 offset, std::string& out)
{
    if (offset >= pool.size()) {
        return false;
    }
    for (usize i = offset; i < pool.size(); ++i) {
        if (pool[i] == std::byte{0}) {
            out.assign(reinterpret_cast<const char*>(pool.data()) + offset, i - offset);
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<core::EngineError> compileMesh(const Model& model, std::span<const TextureSlot> images,
                                             const MeshCompileOptions& options, CompiledMesh& out)
{
    out = CompiledMesh{};

    if (model.mesh.vertices.empty() || model.mesh.indices.empty()) {
        return core::makeError(LUAUG_TR("asset.mesh.err.empty"));
    }
    if (images.size() != model.images.size()) {
        return core::makeError(LUAUG_TR("asset.mesh.err.image_count"));
    }

    out.vertices = model.mesh.vertices;
    out.skin = model.skin;
    out.materials = model.materials;
    out.images.assign(images.begin(), images.end());
    out.joints = model.joints;
    out.clips = model.clips;
    out.bounds = model.mesh.bounds;

    MeshLod base;
    base.indices = model.mesh.indices;
    base.submeshes = model.mesh.submeshes;
    base.error = 0.0f;
    out.lods.push_back(std::move(base));

    const f32* const positions = &out.vertices[0].position.x;
    constexpr usize positionStride = sizeof(Vertex);
    const f32 scale = meshopt_simplifyScale(positions, out.vertices.size(), positionStride);

    // Each level simplifies each SUBMESH separately, so a material boundary is
    // never welded across -- a tree whose leaves start being drawn with the
    // trunk's shader is the failure that buys.
    for (u32 level = 1; level < options.maxLods; ++level) {
        const MeshLod& previous = out.lods.back();
        MeshLod next;
        next.indices.reserve(previous.indices.size());
        next.submeshes.reserve(previous.submeshes.size());

        f32 worstError = 0.0f;
        for (const Submesh& submesh : previous.submeshes) {
            const std::span<const u32> source(previous.indices.data() + submesh.firstIndex, submesh.indexCount);
            const usize target = static_cast<usize>(static_cast<f32>(source.size()) * options.lodStep) / 3 * 3;

            Submesh simplified = submesh;
            simplified.firstIndex = static_cast<u32>(next.indices.size());

            if (target < 3) {
                // Nothing left to remove without deleting the submesh, so it
                // carries through at its current density rather than vanishing.
                next.indices.insert(next.indices.end(), source.begin(), source.end());
                simplified.indexCount = static_cast<u32>(source.size());
                next.submeshes.push_back(simplified);
                continue;
            }

            std::vector<u32> destination(source.size());
            f32 error = 0.0f;
            const usize produced =
                meshopt_simplify(destination.data(), source.data(), source.size(), positions, out.vertices.size(),
                                 positionStride, target, options.lodTargetError, 0, &error);
            destination.resize(produced);

            next.indices.insert(next.indices.end(), destination.begin(), destination.end());
            simplified.indexCount = static_cast<u32>(produced);
            next.submeshes.push_back(simplified);
            worstError = std::max(worstError, error);
        }

        // Two reasons to stop, and both mean "this level is not worth having":
        // it barely removed anything, or it removed too much to still look
        // like the mesh.
        const f32 reduction = 1.0f - static_cast<f32>(next.indices.size()) / static_cast<f32>(previous.indices.size());
        if (next.indices.size() < 3 || reduction < options.lodMinReduction) {
            break;
        }

        // MULTIPLIED by the scale, not divided. `meshopt_simplify` reports an
        // error relative to the mesh's extents and `meshopt_simplifyScale`
        // returns the factor that turns that into absolute units, so this is
        // the level's error in the mesh's own space.
        //
        // It was a division, which is neither relative nor absolute, and nothing
        // had ever read the value -- a number with no consumer is a number with
        // no test. The runtime LOD selector is the first consumer, and it needs
        // absolute: it scales by the instance's transform and divides by the
        // distance to get pixels.
        next.error = worstError * scale;
        out.lods.push_back(std::move(next));
    }

    if (options.buildMeshlets) {
        const std::vector<u32>& indices = out.lods[0].indices;
        const usize bound =
            meshopt_buildMeshletsBound(indices.size(), options.meshletMaxVertices, options.meshletMaxTriangles);
        std::vector<meshopt_Meshlet> raw(bound);
        std::vector<u32> meshletVertices(bound * options.meshletMaxVertices);
        std::vector<unsigned char> meshletTriangles(bound * options.meshletMaxTriangles * 3);

        const usize produced =
            meshopt_buildMeshlets(raw.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(),
                                  indices.size(), positions, out.vertices.size(), positionStride,
                                  options.meshletMaxVertices, options.meshletMaxTriangles, options.meshletConeWeight);
        raw.resize(produced);

        if (produced > 0) {
            const meshopt_Meshlet& last = raw.back();
            meshletVertices.resize(last.vertex_offset + last.vertex_count);
            meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3u));
        }
        else {
            meshletVertices.clear();
            meshletTriangles.clear();
        }

        out.meshlets.vertices = std::move(meshletVertices);
        out.meshlets.triangles.assign(meshletTriangles.begin(), meshletTriangles.end());
        out.meshlets.meshlets.reserve(produced);
        for (const meshopt_Meshlet& entry : raw) {
            const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                out.meshlets.vertices.data() + entry.vertex_offset,
                out.meshlets.triangles.empty()
                    ? nullptr
                    : reinterpret_cast<const unsigned char*>(out.meshlets.triangles.data() + entry.triangle_offset),
                entry.triangle_count, positions, out.vertices.size(), positionStride);

            Meshlet meshlet;
            meshlet.vertexOffset = entry.vertex_offset;
            meshlet.triangleOffset = entry.triangle_offset;
            meshlet.vertexCount = entry.vertex_count;
            meshlet.triangleCount = entry.triangle_count;
            meshlet.center = {bounds.center[0], bounds.center[1], bounds.center[2]};
            meshlet.radius = bounds.radius;
            meshlet.coneAxis = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]};
            meshlet.coneCutoff = bounds.cone_cutoff;
            out.meshlets.meshlets.push_back(meshlet);
        }
    }

    return std::nullopt;
}

std::vector<std::byte> encodeMesh(const CompiledMesh& mesh)
{
    StringPool strings;

    // Section payloads are built first, then laid out. Two passes because a
    // section's offset is not known until every earlier section's length is,
    // and one pass with back-patching would be the same code with a place to
    // make a mistake.
    Writer vertices;
    vertices.raw(encodeVertexStream(mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex)));

    Writer skin;
    if (!mesh.skin.empty()) {
        skin.raw(encodeVertexStream(mesh.skin.data(), mesh.skin.size(), sizeof(SkinVertex)));
    }

    Writer indexData;
    Writer lods;
    Writer submeshes;
    u32 submeshCursor = 0;
    for (const MeshLod& lod : mesh.lods) {
        const std::vector<std::byte> encoded = encodeIndexStream(lod.indices, mesh.vertices.size());
        lods.u32v(static_cast<u32>(lod.indices.size()));
        lods.u32v(submeshCursor);
        lods.u32v(static_cast<u32>(lod.submeshes.size()));
        lods.f32v(lod.error);
        lods.u64v(indexData.size());
        lods.u64v(encoded.size());
        indexData.raw(encoded);

        for (const Submesh& submesh : lod.submeshes) {
            submeshes.u32v(submesh.firstIndex);
            submeshes.u32v(submesh.indexCount);
            submeshes.u32v(submesh.material);
            submeshes.aabb(submesh.bounds);
        }
        submeshCursor += static_cast<u32>(lod.submeshes.size());
    }

    Writer materials;
    for (const MaterialDef& material : mesh.materials) {
        materials.u32v(strings.add(material.name));
        materials.u32v(strings.add(material.shader));
        materials.f32v(material.baseColorFactor.r);
        materials.f32v(material.baseColorFactor.g);
        materials.f32v(material.baseColorFactor.b);
        materials.f32v(material.baseColorAlpha);
        materials.f32v(material.metallicFactor);
        materials.f32v(material.roughnessFactor);
        materials.f32v(material.emissiveFactor.r);
        materials.f32v(material.emissiveFactor.g);
        materials.f32v(material.emissiveFactor.b);
        materials.f32v(material.normalScale);
        materials.u32v(static_cast<u32>(material.alphaMode));
        materials.f32v(material.alphaCutoff);
        materials.u32v(material.doubleSided ? 1u : 0u);
        for (const TextureRef* reference :
             {&material.baseColor, &material.normal, &material.metallicRoughness, &material.emissive}) {
            materials.u32v(reference->image);
            materials.u32v(reference->uvSet);
        }
    }

    Writer images;
    for (const TextureSlot& slot : mesh.images) {
        const std::array<std::byte, 16> hashBytes = core::toBytes(slot.hash);
        images.raw(hashBytes);
        images.u32v(slot.srgb ? 1u : 0u);
        images.u32v(0); // reserved, so the record stays 24 bytes and 4-aligned
    }

    Writer joints;
    for (const Joint& joint : mesh.joints) {
        joints.cframe(joint.localBind);
        joints.mat4(joint.inverseBind);
        joints.u32v(joint.parent);
        joints.u32v(strings.add(joint.name));
    }

    Writer clips;
    Writer channels;
    Writer animationFloats;
    u32 channelCursor = 0;
    for (const AnimationClip& clip : mesh.clips) {
        clips.u32v(strings.add(clip.name));
        clips.f32v(clip.duration);
        clips.u32v(channelCursor);
        clips.u32v(static_cast<u32>(clip.channels.size()));

        for (const AnimationChannel& channel : clip.channels) {
            channels.u32v(channel.joint);
            channels.u32v(static_cast<u32>(channel.target));
            channels.u32v(channel.stride);
            channels.u32v(static_cast<u32>(animationFloats.size() / 4));
            channels.u32v(static_cast<u32>(channel.times.size()));
            for (const f32 time : channel.times) {
                animationFloats.f32v(time);
            }
            channels.u32v(static_cast<u32>(animationFloats.size() / 4));
            channels.u32v(static_cast<u32>(channel.values.size()));
            for (const f32 value : channel.values) {
                animationFloats.f32v(value);
            }
        }
        channelCursor += static_cast<u32>(clip.channels.size());
    }

    Writer meshlets;
    for (const Meshlet& meshlet : mesh.meshlets.meshlets) {
        meshlets.u32v(meshlet.vertexOffset);
        meshlets.u32v(meshlet.triangleOffset);
        meshlets.u32v(meshlet.vertexCount);
        meshlets.u32v(meshlet.triangleCount);
        meshlets.vec3(meshlet.center);
        meshlets.f32v(meshlet.radius);
        meshlets.vec3(meshlet.coneAxis);
        meshlets.f32v(meshlet.coneCutoff);
    }

    Writer meshletVertices;
    for (const u32 index : mesh.meshlets.vertices) {
        meshletVertices.u32v(index);
    }

    Writer meshletTriangles;
    meshletTriangles.raw(std::span<const std::byte>(reinterpret_cast<const std::byte*>(mesh.meshlets.triangles.data()),
                                                    mesh.meshlets.triangles.size()));
    meshletTriangles.pad4();

    struct Pending
    {
        u32 tag;
        std::vector<std::byte>* bytes;
        u32 count;
    };
    std::vector<std::byte> stringBytes(strings.bytes().begin(), strings.bytes().end());
    while ((stringBytes.size() % 4) != 0) {
        stringBytes.push_back(std::byte{0});
    }

    std::vector<Pending> pending = {
        {TagVertices, &vertices.bytes(), static_cast<u32>(mesh.vertices.size())},
        {TagSkin, &skin.bytes(), static_cast<u32>(mesh.skin.size())},
        {TagLods, &lods.bytes(), static_cast<u32>(mesh.lods.size())},
        {TagIndexData, &indexData.bytes(), 0},
        {TagSubmeshes, &submeshes.bytes(), submeshCursor},
        {TagMaterials, &materials.bytes(), static_cast<u32>(mesh.materials.size())},
        {TagImages, &images.bytes(), static_cast<u32>(mesh.images.size())},
        {TagJoints, &joints.bytes(), static_cast<u32>(mesh.joints.size())},
        {TagClips, &clips.bytes(), static_cast<u32>(mesh.clips.size())},
        {TagChannels, &channels.bytes(), channelCursor},
        {TagAnimationFloats, &animationFloats.bytes(), static_cast<u32>(animationFloats.size() / 4)},
        {TagMeshlets, &meshlets.bytes(), static_cast<u32>(mesh.meshlets.meshlets.size())},
        {TagMeshletVertices, &meshletVertices.bytes(), static_cast<u32>(mesh.meshlets.vertices.size())},
        {TagMeshletTriangles, &meshletTriangles.bytes(), static_cast<u32>(mesh.meshlets.triangles.size())},
        {TagStrings, &stringBytes, static_cast<u32>(stringBytes.size())},
    };

    // An empty section is simply absent, so a static mesh's file has no `SKIN`
    // at all rather than a zero-length one every reader has to remember to
    // check.
    std::vector<Pending> present;
    for (Pending& entry : pending) {
        if (!entry.bytes->empty()) {
            present.push_back(entry);
        }
    }
    std::sort(present.begin(), present.end(), [](const Pending& a, const Pending& b) { return a.tag < b.tag; });

    Writer out;
    out.raw(std::span<const std::byte>(reinterpret_cast<const std::byte*>(MeshMagic), 4));
    out.u32v(MeshFormatVersion);
    out.u32v(0); // flags, reserved
    out.u32v(static_cast<u32>(present.size()));
    out.aabb(mesh.bounds);
    // Header is 4 + 4 + 4 + 4 + 24 = 40; the section table follows immediately.

    const usize dataStart = out.size() + present.size() * SectionBytes;
    usize cursor = dataStart;
    for (const Pending& entry : present) {
        out.u32v(entry.tag);
        out.u32v(entry.count);
        out.u64v(cursor);
        out.u64v(entry.bytes->size());
        cursor += entry.bytes->size();
        cursor = (cursor + 3) & ~usize{3};
    }
    for (const Pending& entry : present) {
        out.raw(*entry.bytes);
        out.pad4();
    }
    return std::move(out.bytes());
}

std::optional<core::EngineError> decodeMesh(std::span<const std::byte> bytes, CompiledMesh& out)
{
    out = CompiledMesh{};

    if (bytes.size() < HeaderBytes) {
        return malformed();
    }
    if (std::memcmp(bytes.data(), MeshMagic, 4) != 0) {
        return core::makeError(LUAUG_TR("asset.mesh.err.magic"));
    }

    Reader header(bytes, 4);
    const u32 version = header.u32v();
    if (version != MeshFormatVersion) {
        const I18nArg args[] = {{"found", std::to_string(version)}, {"expected", std::to_string(MeshFormatVersion)}};
        return core::makeError(LUAUG_TR("asset.mesh.err.version"), args);
    }
    if (header.u32v() != 0) {
        return malformed();
    }
    const u32 sectionCount = header.u32v();
    out.bounds = header.aabb();
    if (!header.ok()) {
        return malformed();
    }

    std::vector<Section> sections;
    sections.reserve(sectionCount);
    Reader table(bytes, header.at());
    for (u32 i = 0; i < sectionCount; ++i) {
        Section section;
        section.tag = table.u32v();
        section.count = table.u32v();
        section.offset = table.u64v();
        section.length = table.u64v();
        if (!table.ok()) {
            return malformed();
        }
        // Checked before anything is read through it, and as a subtraction
        // rather than an addition, which is the same statement without an
        // overflow in it.
        if (section.offset > bytes.size() || section.length > bytes.size() - section.offset) {
            return malformed();
        }
        if (i > 0 && !(sections.back().tag < section.tag)) {
            return malformed();
        }
        sections.push_back(section);
    }

    const auto sectionSpan = [&bytes](const Section& section) {
        return bytes.subspan(static_cast<usize>(section.offset), static_cast<usize>(section.length));
    };

    const Section* const vertexSection = findSection(sections, TagVertices);
    if (vertexSection == nullptr || vertexSection->count == 0) {
        return malformed();
    }
    if (vertexSection->count > MaxMeshVertices) {
        return core::makeError(LUAUG_TR("asset.mesh.err.too_large"));
    }
    out.vertices.resize(vertexSection->count);
    {
        const std::span<const std::byte> encoded = sectionSpan(*vertexSection);
        if (meshopt_decodeVertexBuffer(out.vertices.data(), out.vertices.size(), sizeof(Vertex),
                                       reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size()) != 0) {
            return malformed();
        }
    }

    if (const Section* const skinSection = findSection(sections, TagSkin); skinSection != nullptr) {
        // Parallel to the vertex stream by construction, so its count is
        // already bounded by the check above.
        if (skinSection->count != vertexSection->count) {
            return malformed();
        }
        out.skin.resize(skinSection->count);
        const std::span<const std::byte> encoded = sectionSpan(*skinSection);
        if (meshopt_decodeVertexBuffer(out.skin.data(), out.skin.size(), sizeof(SkinVertex),
                                       reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size()) != 0) {
            return malformed();
        }
    }

    const Section* const stringSection = findSection(sections, TagStrings);
    const std::span<const std::byte> strings =
        stringSection != nullptr ? sectionSpan(*stringSection) : std::span<const std::byte>{};

    const Section* const submeshSection = findSection(sections, TagSubmeshes);
    std::vector<Submesh> allSubmeshes;
    if (submeshSection != nullptr) {
        if (!countFits(*submeshSection, SubmeshRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(submeshSection->offset));
        allSubmeshes.reserve(submeshSection->count);
        for (u32 i = 0; i < submeshSection->count; ++i) {
            Submesh submesh;
            submesh.firstIndex = reader.u32v();
            submesh.indexCount = reader.u32v();
            submesh.material = reader.u32v();
            submesh.bounds = reader.aabb();
            allSubmeshes.push_back(submesh);
        }
        if (!reader.ok()) {
            return malformed();
        }
    }

    const Section* const lodSection = findSection(sections, TagLods);
    const Section* const indexSection = findSection(sections, TagIndexData);
    if (lodSection == nullptr || lodSection->count == 0 || indexSection == nullptr ||
        !countFits(*lodSection, LodRecordBytes)) {
        return malformed();
    }
    {
        const std::span<const std::byte> indexBytes = sectionSpan(*indexSection);
        Reader reader(bytes, static_cast<usize>(lodSection->offset));
        out.lods.reserve(lodSection->count);
        for (u32 i = 0; i < lodSection->count; ++i) {
            const u32 indexCount = reader.u32v();
            const u32 firstSubmesh = reader.u32v();
            const u32 submeshCount = reader.u32v();
            const f32 error = reader.f32v();
            const u64 dataOffset = reader.u64v();
            const u64 dataLength = reader.u64v();
            if (!reader.ok()) {
                return malformed();
            }
            if (dataOffset > indexBytes.size() || dataLength > indexBytes.size() - dataOffset) {
                return malformed();
            }
            if (firstSubmesh > allSubmeshes.size() || submeshCount > allSubmeshes.size() - firstSubmesh) {
                return malformed();
            }

            if (indexCount > MaxMeshVertices * 3u) {
                return core::makeError(LUAUG_TR("asset.mesh.err.too_large"));
            }

            MeshLod lod;
            lod.error = error;
            lod.indices.resize(indexCount);
            if (indexCount > 0) {
                const std::span<const std::byte> encoded =
                    indexBytes.subspan(static_cast<usize>(dataOffset), static_cast<usize>(dataLength));
                if (meshopt_decodeIndexBuffer(lod.indices.data(), lod.indices.size(), sizeof(u32),
                                              reinterpret_cast<const unsigned char*>(encoded.data()),
                                              encoded.size()) != 0) {
                    return malformed();
                }
            }
            lod.submeshes.assign(allSubmeshes.begin() + firstSubmesh,
                                 allSubmeshes.begin() + firstSubmesh + submeshCount);
            out.lods.push_back(std::move(lod));
        }
    }

    if (const Section* const materialSection = findSection(sections, TagMaterials); materialSection != nullptr) {
        if (!countFits(*materialSection, MaterialRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(materialSection->offset));
        out.materials.reserve(materialSection->count);
        for (u32 i = 0; i < materialSection->count; ++i) {
            MaterialDef material;
            const u32 nameOffset = reader.u32v();
            const u32 shaderOffset = reader.u32v();
            material.baseColorFactor.r = reader.f32v();
            material.baseColorFactor.g = reader.f32v();
            material.baseColorFactor.b = reader.f32v();
            material.baseColorAlpha = reader.f32v();
            material.metallicFactor = reader.f32v();
            material.roughnessFactor = reader.f32v();
            material.emissiveFactor.r = reader.f32v();
            material.emissiveFactor.g = reader.f32v();
            material.emissiveFactor.b = reader.f32v();
            material.normalScale = reader.f32v();
            const u32 alphaMode = reader.u32v();
            material.alphaCutoff = reader.f32v();
            material.doubleSided = reader.u32v() != 0;
            for (TextureRef* reference :
                 {&material.baseColor, &material.normal, &material.metallicRoughness, &material.emissive}) {
                reference->image = reader.u32v();
                reference->uvSet = reader.u32v();
            }
            if (!reader.ok() || alphaMode > static_cast<u32>(AlphaMode::Blend)) {
                return malformed();
            }
            material.alphaMode = static_cast<AlphaMode>(alphaMode);
            if (!poolString(strings, nameOffset, material.name) ||
                !poolString(strings, shaderOffset, material.shader)) {
                return malformed();
            }
            out.materials.push_back(std::move(material));
        }
    }

    if (const Section* const imageSection = findSection(sections, TagImages); imageSection != nullptr) {
        if (!countFits(*imageSection, ImageRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(imageSection->offset));
        out.images.reserve(imageSection->count);
        for (u32 i = 0; i < imageSection->count; ++i) {
            const usize at = reader.at();
            if (!reader.has(16)) {
                return malformed();
            }
            TextureSlot slot;
            slot.hash = core::fromBytes(std::span<const std::byte, 16>(bytes.data() + at, 16));
            Reader after(bytes, at + 16);
            slot.srgb = after.u32v() != 0;
            (void)after.u32v();
            if (!after.ok()) {
                return malformed();
            }
            reader = Reader(bytes, after.at());
            out.images.push_back(slot);
        }
    }

    if (const Section* const jointSection = findSection(sections, TagJoints); jointSection != nullptr) {
        if (!countFits(*jointSection, JointRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(jointSection->offset));
        out.joints.reserve(jointSection->count);
        for (u32 i = 0; i < jointSection->count; ++i) {
            Joint joint;
            joint.localBind = reader.cframe();
            joint.inverseBind = reader.mat4();
            joint.parent = reader.u32v();
            const u32 nameOffset = reader.u32v();
            if (!reader.ok() || !poolString(strings, nameOffset, joint.name)) {
                return malformed();
            }
            // The loader's contract, restated where it can be checked: a
            // parent is always earlier in the array, which is what lets a pose
            // resolve in one forward pass.
            if (joint.parent != Joint::NoParent && joint.parent >= i) {
                return malformed();
            }
            out.joints.push_back(std::move(joint));
        }
    }

    const Section* const floatSection = findSection(sections, TagAnimationFloats);
    const std::span<const std::byte> floatBytes =
        floatSection != nullptr ? sectionSpan(*floatSection) : std::span<const std::byte>{};
    const usize floatCount = floatBytes.size() / 4;

    const auto readFloats = [&](u32 offset, u32 count, std::vector<f32>& target) {
        if (offset > floatCount || count > floatCount - offset) {
            return false;
        }
        Reader reader(floatBytes, static_cast<usize>(offset) * 4);
        target.resize(count);
        for (u32 i = 0; i < count; ++i) {
            target[i] = reader.f32v();
        }
        return reader.ok();
    };

    std::vector<AnimationChannel> allChannels;
    if (const Section* const channelSection = findSection(sections, TagChannels); channelSection != nullptr) {
        if (!countFits(*channelSection, ChannelRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(channelSection->offset));
        allChannels.reserve(channelSection->count);
        for (u32 i = 0; i < channelSection->count; ++i) {
            AnimationChannel channel;
            channel.joint = reader.u32v();
            const u32 target = reader.u32v();
            channel.stride = reader.u32v();
            const u32 timesOffset = reader.u32v();
            const u32 timesCount = reader.u32v();
            const u32 valuesOffset = reader.u32v();
            const u32 valuesCount = reader.u32v();
            if (!reader.ok() || target > static_cast<u32>(AnimationChannel::Target::Scale)) {
                return malformed();
            }
            channel.target = static_cast<AnimationChannel::Target>(target);
            if (!readFloats(timesOffset, timesCount, channel.times) ||
                !readFloats(valuesOffset, valuesCount, channel.values)) {
                return malformed();
            }
            allChannels.push_back(std::move(channel));
        }
    }

    if (const Section* const clipSection = findSection(sections, TagClips); clipSection != nullptr) {
        if (!countFits(*clipSection, ClipRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(clipSection->offset));
        out.clips.reserve(clipSection->count);
        for (u32 i = 0; i < clipSection->count; ++i) {
            AnimationClip clip;
            const u32 nameOffset = reader.u32v();
            clip.duration = reader.f32v();
            const u32 firstChannel = reader.u32v();
            const u32 channelCount = reader.u32v();
            if (!reader.ok() || !poolString(strings, nameOffset, clip.name)) {
                return malformed();
            }
            if (firstChannel > allChannels.size() || channelCount > allChannels.size() - firstChannel) {
                return malformed();
            }
            clip.channels.assign(allChannels.begin() + firstChannel, allChannels.begin() + firstChannel + channelCount);
            out.clips.push_back(std::move(clip));
        }
    }

    if (const Section* const meshletVertexSection = findSection(sections, TagMeshletVertices);
        meshletVertexSection != nullptr) {
        if (!countFits(*meshletVertexSection, 4)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(meshletVertexSection->offset));
        out.meshlets.vertices.resize(meshletVertexSection->count);
        for (u32& index : out.meshlets.vertices) {
            index = reader.u32v();
        }
        if (!reader.ok()) {
            return malformed();
        }
    }

    if (const Section* const meshletTriangleSection = findSection(sections, TagMeshletTriangles);
        meshletTriangleSection != nullptr) {
        const std::span<const std::byte> raw = sectionSpan(*meshletTriangleSection);
        if (meshletTriangleSection->count > raw.size()) {
            return malformed();
        }
        out.meshlets.triangles.resize(meshletTriangleSection->count);
        std::memcpy(out.meshlets.triangles.data(), raw.data(), meshletTriangleSection->count);
    }

    if (const Section* const meshletSection = findSection(sections, TagMeshlets); meshletSection != nullptr) {
        if (!countFits(*meshletSection, MeshletRecordBytes)) {
            return malformed();
        }
        Reader reader(bytes, static_cast<usize>(meshletSection->offset));
        out.meshlets.meshlets.reserve(meshletSection->count);
        for (u32 i = 0; i < meshletSection->count; ++i) {
            Meshlet meshlet;
            meshlet.vertexOffset = reader.u32v();
            meshlet.triangleOffset = reader.u32v();
            meshlet.vertexCount = reader.u32v();
            meshlet.triangleCount = reader.u32v();
            meshlet.center = reader.vec3();
            meshlet.radius = reader.f32v();
            meshlet.coneAxis = reader.vec3();
            meshlet.coneCutoff = reader.f32v();
            if (!reader.ok()) {
                return malformed();
            }
            if (meshlet.vertexOffset > out.meshlets.vertices.size() ||
                meshlet.vertexCount > out.meshlets.vertices.size() - meshlet.vertexOffset) {
                return malformed();
            }
            out.meshlets.meshlets.push_back(meshlet);
        }
    }

    // The one cross-section invariant worth enforcing at load: an index that
    // names a vertex the file does not contain is a draw that reads whatever
    // is next in GPU memory.
    for (const MeshLod& lod : out.lods) {
        for (const u32 index : lod.indices) {
            if (index >= out.vertices.size()) {
                return malformed();
            }
        }
        for (const Submesh& submesh : lod.submeshes) {
            if (submesh.firstIndex > lod.indices.size() ||
                submesh.indexCount > lod.indices.size() - submesh.firstIndex ||
                submesh.material >= std::max<usize>(out.materials.size(), 1)) {
                return malformed();
            }
        }
    }

    return std::nullopt;
}

} // namespace luaug::asset
