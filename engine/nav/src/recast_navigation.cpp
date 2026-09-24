// Navigation over Recast and Detour (ADR 0089).
//
// One `dtNavMesh` of square tiles, each built by Recast from the static
// geometry that overlaps it, and one `dtNavMeshQuery` over it. The one piece of
// policy here that is not Recast's is WHEN a tile is built: when a query needs
// it and either nothing is there yet or what stands in it changed -- which is
// what the per-tile fingerprint answers.
#include "luaug/asset/terrain_mesher.h"
#include "luaug/nav/nav.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace luaug::nav {
namespace {

using core::DVec3;
using core::f64;
using core::i32;
using core::Vec3;

// Cells along a tile's side. With a cell a quarter of a metre -- a half-metre
// agent's -- a tile is 32 metres: large enough that a room is one tile, small
// enough that a wall moving rebuilds a courtyard and not a district.
constexpr int TileCells = 128;
// How many tiles one query may build. A path across a continent of ground no
// query has touched comes back partial rather than stalling the tick; the next
// query from where it stopped builds the next stretch.
constexpr usize QueryTileBudget = 24;
constexpr usize RegionTileBudget = 1024;
// The mesh holds up to this many tiles and this many polygons in each; a
// 32-bit poly reference splits into both plus a salt of at least ten bits.
constexpr int MaxTiles = 4096;
constexpr int MaxPolysPerTile = 1024;
constexpr int MaxPathPolys = 2048;
constexpr int MaxStraightPoints = 256;
constexpr unsigned short WalkableFlag = 1;

// FNV-1a, folded over the bytes of what a tile's mesh depends on.
struct Fingerprint
{
    u64 value = 1469598103934665603ull;

    template <class T>
    void add(const T& item) noexcept
    {
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &item, sizeof(T));
        for (const unsigned char byte : bytes) {
            value ^= byte;
            value *= 1099511628211ull;
        }
    }
};

// A piece of static geometry, found once per tick: its footprint on the
// ground plane, and a hash of everything about it the mesh depends on.
struct Static
{
    enum class Kind : unsigned char
    {
        Part,
        Terrain,
    };
    Kind kind = Kind::Part;
    core::InstanceId id;
    f64 minX = 0.0;
    f64 minZ = 0.0;
    f64 maxX = 0.0;
    f64 maxZ = 0.0;
    u64 hash = 0;
};

struct TileKey
{
    i32 x = 0;
    i32 z = 0;
    [[nodiscard]] constexpr auto operator<=>(const TileKey&) const noexcept = default;
};

struct TileRecord
{
    u64 fingerprint = 0;
    dtTileRef ref = 0;
};

// Triangles for Recast: flat positions and index triples.
struct Soup
{
    std::vector<float> vertices;
    std::vector<int> triangles;

    [[nodiscard]] int vertex(const DVec3& at)
    {
        const int index = static_cast<int>(vertices.size() / 3);
        vertices.push_back(static_cast<float>(at.x));
        vertices.push_back(static_cast<float>(at.y));
        vertices.push_back(static_cast<float>(at.z));
        return index;
    }

    // Wound so Recast's normal -- (b - a) x (c - a) -- points along `outward`,
    // which is what `rcMarkWalkableTriangles` reads a floor from.
    void triangle(int a, int b, int c, const Vec3& outward)
    {
        const auto at = [this](int index) {
            const auto base = static_cast<usize>(index) * 3;
            return Vec3{vertices[base], vertices[base + 1], vertices[base + 2]};
        };
        const Vec3 normal = core::cross(at(b) - at(a), at(c) - at(a));
        if (core::dot(normal, outward) < 0.0f)
            std::swap(b, c);
        triangles.insert(triangles.end(), {a, b, c});
    }
};

[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId workspace, core::InstanceId id) noexcept
{
    return workspace.valid() && world.isAncestorOf(workspace, id);
}

// The eight corners of a part's box, in the world.
[[nodiscard]] std::array<DVec3, 8> cornersOf(const core::CFrameD& frame, Vec3 size) noexcept
{
    std::array<DVec3, 8> corners{};
    const Vec3 half = size * 0.5f;
    for (int at = 0; at < 8; ++at) {
        const Vec3 local{(at & 1) != 0 ? half.x : -half.x, (at & 2) != 0 ? half.y : -half.y,
                         (at & 4) != 0 ? half.z : -half.z};
        corners[static_cast<usize>(at)] = frame.position + core::toDVec3(frame.rotation * local);
    }
    return corners;
}

class RecastNavigation final : public INavigation
{
public:
    explicit RecastNavigation(const scene::World& world) : m_world(world) {}

    ~RecastNavigation() override
    {
        if (m_query != nullptr)
            dtFreeNavMeshQuery(m_query);
        if (m_mesh != nullptr)
            dtFreeNavMesh(m_mesh);
    }

    RecastNavigation(const RecastNavigation&) = delete;
    RecastNavigation& operator=(const RecastNavigation&) = delete;

    void setWorkspace(core::InstanceId workspace) noexcept override
    {
        if (workspace != m_workspace) {
            m_workspace = workspace;
            m_gathered = false;
        }
    }

    void setAgent(const NavAgent& agent) override
    {
        if (agent == m_agent)
            return;
        m_agent = agent;
        invalidate();
    }

    void setTick(u64 tick) noexcept override { m_tick = tick; }

    [[nodiscard]] std::optional<NavPath> findPath(DVec3 from, DVec3 to) override
    {
        prepare();
        ensureTiles(std::min(from.x, to.x), std::min(from.z, to.z), std::max(from.x, to.x), std::max(from.z, to.z),
                    tileMetres(), QueryTileBudget);
        if (!ready())
            return std::nullopt;

        const dtQueryFilter filter = walkable();
        float start[3];
        dtPolyRef startRef = 0;
        if (!nearest(from, searchExtents(), filter, startRef, start))
            return std::nullopt;

        NavPath path;
        float goal[3];
        dtPolyRef goalRef = 0;
        bool goalOnMesh = nearest(to, searchExtents(), filter, goalRef, goal);
        if (!goalOnMesh) {
            // The goal is off the mesh: head for the nearest ground to it that
            // is, and say the path is not the whole way.
            const float wide[3] = {8.0f, 16.0f, 8.0f};
            if (!nearest(to, wide, filter, goalRef, goal)) {
                path.points.push_back(pointOf(start));
                return path;
            }
        }

        std::array<dtPolyRef, MaxPathPolys> polys{};
        int count = 0;
        const dtStatus status =
            m_query->findPath(startRef, goalRef, start, goal, &filter, polys.data(), &count, MaxPathPolys);
        if (dtStatusFailed(status) || count == 0)
            return std::nullopt;

        // A partial search ends on the polygon nearest the goal it reached;
        // the straight path runs to the closest point on it.
        const bool reached =
            polys[static_cast<usize>(count - 1)] == goalRef && !dtStatusDetail(status, DT_PARTIAL_RESULT);
        float target[3] = {goal[0], goal[1], goal[2]};
        if (!reached)
            m_query->closestPointOnPoly(polys[static_cast<usize>(count - 1)], goal, target, nullptr);

        std::array<float, MaxStraightPoints * 3> straight{};
        int points = 0;
        m_query->findStraightPath(start, target, polys.data(), count, straight.data(), nullptr, nullptr, &points,
                                  MaxStraightPoints, 0);
        path.points.reserve(static_cast<usize>(points));
        for (int at = 0; at < points; ++at)
            path.points.push_back(pointOf(&straight[static_cast<usize>(at) * 3]));
        path.complete = reached && goalOnMesh;
        return path;
    }

    [[nodiscard]] std::optional<DVec3> nearestPoint(DVec3 point, f32 maxDistance) override
    {
        prepare();
        const f64 reach = static_cast<f64>(std::max(maxDistance, 0.0f));
        ensureTiles(point.x - reach, point.z - reach, point.x + reach, point.z + reach, 0.0, QueryTileBudget);
        if (!ready())
            return std::nullopt;
        const float extents[3] = {std::max(maxDistance, 0.01f), std::max(maxDistance, 0.01f),
                                  std::max(maxDistance, 0.01f)};
        const dtQueryFilter filter = walkable();
        dtPolyRef ref = 0;
        float found[3];
        if (!nearest(point, extents, filter, ref, found))
            return std::nullopt;
        const DVec3 at = pointOf(found);
        const DVec3 offset = at - point;
        if (offset.x * offset.x + offset.y * offset.y + offset.z * offset.z > reach * reach + 1e-6)
            return std::nullopt;
        return at;
    }

    [[nodiscard]] std::optional<DVec3> raycast(DVec3 from, DVec3 to) override
    {
        prepare();
        ensureTiles(std::min(from.x, to.x), std::min(from.z, to.z), std::max(from.x, to.x), std::max(from.z, to.z),
                    tileMetres(), QueryTileBudget);
        if (!ready())
            return std::nullopt;
        const dtQueryFilter filter = walkable();
        dtPolyRef startRef = 0;
        float start[3];
        if (!nearest(from, searchExtents(), filter, startRef, start))
            return std::nullopt;
        const float end[3] = {static_cast<float>(to.x), start[1], static_cast<float>(to.z)};
        float along = 0.0f;
        float normal[3];
        std::array<dtPolyRef, MaxPathPolys> polys{};
        int count = 0;
        if (dtStatusFailed(
                m_query->raycast(startRef, start, end, &filter, &along, normal, polys.data(), &count, MaxPathPolys)))
            return std::nullopt;
        // Past the end means nothing stopped it.
        const float t = std::min(along, 1.0f);
        float stop[3] = {start[0] + (end[0] - start[0]) * t, start[1], start[2] + (end[2] - start[2]) * t};
        // Onto the mesh's own height where it stopped.
        if (count > 0) {
            float height = stop[1];
            if (dtStatusSucceed(m_query->getPolyHeight(polys[static_cast<usize>(count - 1)], stop, &height)))
                stop[1] = height;
        }
        return pointOf(stop);
    }

    usize buildRegion(DVec3 minimum, DVec3 maximum) override
    {
        prepare();
        return ensureTiles(std::min(minimum.x, maximum.x), std::min(minimum.z, maximum.z),
                           std::max(minimum.x, maximum.x), std::max(minimum.z, maximum.z), 0.0, RegionTileBudget);
    }

    void invalidate() override
    {
        if (m_query != nullptr)
            dtFreeNavMeshQuery(m_query);
        if (m_mesh != nullptr)
            dtFreeNavMesh(m_mesh);
        m_query = nullptr;
        m_mesh = nullptr;
        m_tiles.clear();
    }

    [[nodiscard]] usize tileCount() const noexcept override
    {
        usize count = 0;
        for (const auto& [key, record] : m_tiles)
            count += record.ref != 0 ? 1 : 0;
        return count;
    }

private:
    [[nodiscard]] float cellSize() const noexcept { return std::clamp(m_agent.radius * 0.5f, 0.05f, 0.5f); }
    [[nodiscard]] float cellHeight() const noexcept { return std::max(cellSize() * 0.5f, 0.025f); }
    [[nodiscard]] f64 tileMetres() const noexcept { return static_cast<f64>(cellSize()) * TileCells; }

    [[nodiscard]] bool ready() const noexcept { return m_mesh != nullptr && m_query != nullptr; }

    [[nodiscard]] static dtQueryFilter walkable() noexcept
    {
        dtQueryFilter filter;
        filter.setIncludeFlags(WalkableFlag);
        filter.setExcludeFlags(0);
        return filter;
    }

    // How far above and below a point to look for the mesh under it: an agent
    // standing is its height above the ground, and one jumping more.
    [[nodiscard]] const float* searchExtents() noexcept
    {
        m_extents[0] = std::max(m_agent.radius * 2.0f, 1.0f);
        m_extents[1] = std::max(m_agent.height * 1.5f, 2.0f);
        m_extents[2] = m_extents[0];
        return m_extents.data();
    }

    [[nodiscard]] bool nearest(DVec3 point, const float* extents, const dtQueryFilter& filter, dtPolyRef& ref,
                               float* out) const
    {
        const float centre[3] = {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
        ref = 0;
        if (dtStatusFailed(m_query->findNearestPoly(centre, extents, &filter, &ref, out)))
            return false;
        return ref != 0;
    }

    [[nodiscard]] static DVec3 pointOf(const float* at) noexcept
    {
        return DVec3{static_cast<f64>(at[0]), static_cast<f64>(at[1]), static_cast<f64>(at[2])};
    }

    // The mesh and its query, made on first use and after an invalidate.
    void prepare()
    {
        gather();
        if (m_mesh != nullptr)
            return;
        m_mesh = dtAllocNavMesh();
        dtNavMeshParams params{};
        params.orig[0] = 0.0f;
        params.orig[1] = 0.0f;
        params.orig[2] = 0.0f;
        params.tileWidth = static_cast<float>(tileMetres());
        params.tileHeight = static_cast<float>(tileMetres());
        params.maxTiles = MaxTiles;
        params.maxPolys = MaxPolysPerTile;
        m_query = dtAllocNavMeshQuery();
        if (m_mesh == nullptr || m_query == nullptr || dtStatusFailed(m_mesh->init(&params)) ||
            dtStatusFailed(m_query->init(m_mesh, 4096))) {
            invalidate();
        }
    }

    // **What stands in the world, gathered again only when it may differ**:
    // on a new tick, after a write through the world's own verbs, or after a
    // terrain edit. So a script that builds a wall and asks for a path in the
    // same breath gets a path round the wall, and a thousand agents asking in
    // one tick that wrote nothing cost one gather.
    void gather()
    {
        Fingerprint terrains;
        m_world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
            terrains.add(id.index);
            terrains.add(terrain.fieldRevision);
            terrains.add(terrain.origin);
        });
        const u64 mutations = m_world.mutations();
        if (m_gathered && m_gatheredTick == m_tick && m_gatheredMutations == mutations &&
            m_gatheredTerrains == terrains.value) {
            return;
        }
        m_gathered = true;
        m_gatheredTick = m_tick;
        m_gatheredMutations = mutations;
        m_gatheredTerrains = terrains.value;
        m_statics.clear();

        m_world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
            if (!inWorld(m_world, m_workspace, id))
                return;
            const scene::RigidBodyComponent* body = m_world.rigidBodies().find(id);
            if (body == nullptr || !body->anchored || !body->canCollide)
                return;
            if (m_world.characterBodies().find(id) != nullptr)
                return;
            Static entry;
            entry.kind = Static::Kind::Part;
            entry.id = id;
            entry.minX = std::numeric_limits<f64>::max();
            entry.minZ = std::numeric_limits<f64>::max();
            entry.maxX = std::numeric_limits<f64>::lowest();
            entry.maxZ = std::numeric_limits<f64>::lowest();
            for (const DVec3& corner : cornersOf(part.cframe, part.size)) {
                entry.minX = std::min(entry.minX, corner.x);
                entry.minZ = std::min(entry.minZ, corner.z);
                entry.maxX = std::max(entry.maxX, corner.x);
                entry.maxZ = std::max(entry.maxZ, corner.z);
            }
            Fingerprint hash;
            hash.add(id.index);
            hash.add(id.generation);
            hash.add(part.cframe.position);
            hash.add(part.cframe.rotation);
            hash.add(part.size);
            entry.hash = hash.value;
            m_statics.push_back(entry);
        });

        m_world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
            if (!inWorld(m_world, m_workspace, id) || terrain.field.empty())
                return;
            Static entry;
            entry.kind = Static::Kind::Terrain;
            entry.id = id;
            entry.minX = std::numeric_limits<f64>::lowest();
            entry.minZ = std::numeric_limits<f64>::lowest();
            entry.maxX = std::numeric_limits<f64>::max();
            entry.maxZ = std::numeric_limits<f64>::max();
            Fingerprint hash;
            hash.add(id.index);
            hash.add(terrain.fieldRevision);
            hash.add(terrain.origin);
            entry.hash = hash.value;
            m_statics.push_back(entry);
        });
    }

    // The footprint a tile reads geometry from: the tile and the border Recast
    // erodes into, so a wall just over the line still narrows the path.
    void tileBounds(TileKey key, f64& minX, f64& minZ, f64& maxX, f64& maxZ) const noexcept
    {
        const f64 size = tileMetres();
        const f64 border = static_cast<f64>(borderCells()) * static_cast<f64>(cellSize());
        minX = static_cast<f64>(key.x) * size - border;
        minZ = static_cast<f64>(key.z) * size - border;
        maxX = static_cast<f64>(key.x + 1) * size + border;
        maxZ = static_cast<f64>(key.z + 1) * size + border;
    }

    [[nodiscard]] int walkableRadiusCells() const noexcept
    {
        return static_cast<int>(std::ceil(m_agent.radius / cellSize()));
    }
    [[nodiscard]] int borderCells() const noexcept { return walkableRadiusCells() + 3; }

    [[nodiscard]] u64 fingerprintOf(TileKey key) const noexcept
    {
        f64 minX = 0.0;
        f64 minZ = 0.0;
        f64 maxX = 0.0;
        f64 maxZ = 0.0;
        tileBounds(key, minX, minZ, maxX, maxZ);
        Fingerprint hash;
        hash.add(key.x);
        hash.add(key.z);
        for (const Static& entry : m_statics) {
            if (entry.maxX < minX || entry.minX > maxX || entry.maxZ < minZ || entry.minZ > maxZ)
                continue;
            hash.add(entry.hash);
        }
        return hash.value;
    }

    // Builds whatever the rectangle (grown by `margin`) needs, in tile order,
    // until the budget runs out. Answers how many it built.
    usize ensureTiles(f64 minX, f64 minZ, f64 maxX, f64 maxZ, f64 margin, usize budget)
    {
        if (m_mesh == nullptr)
            return 0;
        const f64 size = tileMetres();
        const auto low = [&](f64 value) { return static_cast<i32>(std::floor((value - margin) / size)); };
        const auto high = [&](f64 value) { return static_cast<i32>(std::floor((value + margin) / size)); };
        // A rectangle past the mesh's capacity is refused whole rather than
        // built in part: a corner of a continent is not a navmesh.
        const i32 x0 = low(minX);
        const i32 z0 = low(minZ);
        const i32 x1 = high(maxX);
        const i32 z1 = high(maxZ);
        if (static_cast<f64>(x1 - x0 + 1) * static_cast<f64>(z1 - z0 + 1) > static_cast<f64>(MaxTiles))
            return 0;
        usize built = 0;
        for (i32 z = z0; z <= z1; ++z) {
            for (i32 x = x0; x <= x1; ++x) {
                const TileKey key{x, z};
                const u64 fingerprint = fingerprintOf(key);
                const auto found = m_tiles.find(key);
                if (found != m_tiles.end() && found->second.fingerprint == fingerprint)
                    continue;
                if (built >= budget)
                    return built;
                buildTile(key, fingerprint);
                ++built;
            }
        }
        return built;
    }

    void soupOf(TileKey key, Soup& soup) const
    {
        f64 minX = 0.0;
        f64 minZ = 0.0;
        f64 maxX = 0.0;
        f64 maxZ = 0.0;
        tileBounds(key, minX, minZ, maxX, maxZ);
        for (const Static& entry : m_statics) {
            if (entry.maxX < minX || entry.minX > maxX || entry.maxZ < minZ || entry.minZ > maxZ)
                continue;
            if (entry.kind == Static::Kind::Part)
                partSoup(entry.id, soup);
            else
                terrainSoup(entry.id, minX, minZ, maxX, maxZ, soup);
        }
    }

    // A part as its box: the collider's own approximation for every shape it
    // does not model exactly.
    void partSoup(core::InstanceId id, Soup& soup) const
    {
        const scene::PartComponent* part = m_world.parts().find(id);
        if (part == nullptr)
            return;
        const std::array<DVec3, 8> corners = cornersOf(part->cframe, part->size);
        std::array<int, 8> index{};
        for (usize at = 0; at < 8; ++at)
            index[at] = soup.vertex(corners[at]);
        // Faces by the axis bit they share, each with its outward normal.
        const core::Mat3& basis = part->cframe.rotation;
        const Vec3 axes[3] = {Vec3{basis.m[0][0], basis.m[0][1], basis.m[0][2]},
                              Vec3{basis.m[1][0], basis.m[1][1], basis.m[1][2]},
                              Vec3{basis.m[2][0], basis.m[2][1], basis.m[2][2]}};
        for (int axis = 0; axis < 3; ++axis) {
            const int bit = 1 << axis;
            const int u = 1 << ((axis + 1) % 3);
            const int v = 1 << ((axis + 2) % 3);
            for (const int side : {0, bit}) {
                const Vec3 outward = side != 0 ? axes[axis] : axes[axis] * -1.0f;
                const int a = index[static_cast<usize>(side)];
                const int b = index[static_cast<usize>(side | u)];
                const int c = index[static_cast<usize>(side | u | v)];
                const int d = index[static_cast<usize>(side | v)];
                soup.triangle(a, b, c, outward);
                soup.triangle(a, c, d, outward);
            }
        }
    }

    // The terrain's surface over the tile, from the mesher the renderer and
    // the collider use -- so an agent walks the ground that is drawn.
    void terrainSoup(core::InstanceId id, f64 minX, f64 minZ, f64 maxX, f64 maxZ, Soup& soup) const
    {
        const scene::TerrainComponent* terrain = m_world.terrains().find(id);
        if (terrain == nullptr || terrain->field.empty())
            return;
        const asset::TerrainField& field = terrain->field;
        const f64 voxel = static_cast<f64>(field.settings().voxelSize);
        const f64 span = voxel * static_cast<f64>(asset::ChunkEdge);
        const auto chunk = [&](f64 value, f64 origin) { return static_cast<i32>(std::floor((value - origin) / span)); };
        const i32 cx0 = chunk(minX, terrain->origin.x);
        const i32 cx1 = chunk(maxX, terrain->origin.x);
        const i32 cz0 = chunk(minZ, terrain->origin.z);
        const i32 cz1 = chunk(maxZ, terrain->origin.z);
        for (i32 cz = cz0; cz <= cz1; ++cz) {
            for (i32 cx = cx0; cx <= cx1; ++cx) {
                for (const auto& [low, high] : asset::activeRuns(field, cx, cz, 1)) {
                    asset::MeshRegion region;
                    region.minX = cx * static_cast<i32>(asset::ChunkEdge);
                    region.minZ = cz * static_cast<i32>(asset::ChunkEdge);
                    region.minY = low;
                    region.cellsX = asset::ChunkEdge;
                    region.cellsZ = asset::ChunkEdge;
                    region.cellsY = static_cast<core::u32>(high - low + 1);
                    const asset::TerrainMesh meshed = asset::meshField(field, region);
                    const int first = static_cast<int>(soup.vertices.size() / 3);
                    for (const Vec3& point : meshed.colliderPoints)
                        (void)soup.vertex(terrain->origin + core::toDVec3(point));
                    for (usize at = 0; at + 2 < meshed.colliderIndices.size(); at += 3) {
                        soup.triangles.insert(soup.triangles.end(),
                                              {first + static_cast<int>(meshed.colliderIndices[at]),
                                               first + static_cast<int>(meshed.colliderIndices[at + 1]),
                                               first + static_cast<int>(meshed.colliderIndices[at + 2])});
                    }
                }
            }
        }
    }

    void buildTile(TileKey key, u64 fingerprint)
    {
        TileRecord& record = m_tiles[key];
        if (record.ref != 0) {
            (void)m_mesh->removeTile(record.ref, nullptr, nullptr);
            record.ref = 0;
        }
        record.fingerprint = fingerprint;

        Soup soup;
        soupOf(key, soup);
        if (soup.triangles.empty())
            return;

        rcConfig config{};
        config.cs = cellSize();
        config.ch = cellHeight();
        config.walkableSlopeAngle = m_agent.maxSlope;
        config.walkableHeight = static_cast<int>(std::ceil(m_agent.height / config.ch));
        config.walkableClimb = static_cast<int>(std::floor(m_agent.maxClimb / config.ch));
        config.walkableRadius = walkableRadiusCells();
        config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
        config.maxSimplificationError = 1.3f;
        config.minRegionArea = 8 * 8;
        config.mergeRegionArea = 20 * 20;
        config.maxVertsPerPoly = 6;
        config.tileSize = TileCells;
        config.borderSize = borderCells();
        config.width = TileCells + config.borderSize * 2;
        config.height = TileCells + config.borderSize * 2;
        config.detailSampleDist = config.cs * 6.0f;
        config.detailSampleMaxError = config.ch;

        const f64 size = tileMetres();
        const float border = static_cast<float>(config.borderSize) * config.cs;
        float lowY = std::numeric_limits<float>::max();
        float highY = std::numeric_limits<float>::lowest();
        for (usize at = 1; at < soup.vertices.size(); at += 3) {
            lowY = std::min(lowY, soup.vertices[at]);
            highY = std::max(highY, soup.vertices[at]);
        }
        config.bmin[0] = static_cast<float>(static_cast<f64>(key.x) * size) - border;
        config.bmin[1] = lowY - 1.0f;
        config.bmin[2] = static_cast<float>(static_cast<f64>(key.z) * size) - border;
        config.bmax[0] = static_cast<float>(static_cast<f64>(key.x + 1) * size) + border;
        config.bmax[1] = highY + m_agent.height + 1.0f;
        config.bmax[2] = static_cast<float>(static_cast<f64>(key.z + 1) * size) + border;

        rcContext context(false);
        const int vertexCount = static_cast<int>(soup.vertices.size() / 3);
        const int triangleCount = static_cast<int>(soup.triangles.size() / 3);

        // Every Recast object freed on every path out.
        struct Owned
        {
            rcHeightfield* heights = nullptr;
            rcCompactHeightfield* compact = nullptr;
            rcContourSet* contours = nullptr;
            rcPolyMesh* polys = nullptr;
            rcPolyMeshDetail* detail = nullptr;
            ~Owned()
            {
                rcFreeHeightField(heights);
                rcFreeCompactHeightfield(compact);
                rcFreeContourSet(contours);
                rcFreePolyMesh(polys);
                rcFreePolyMeshDetail(detail);
            }
        } owned;

        owned.heights = rcAllocHeightfield();
        if (owned.heights == nullptr || !rcCreateHeightfield(&context, *owned.heights, config.width, config.height,
                                                             config.bmin, config.bmax, config.cs, config.ch))
            return;
        std::vector<unsigned char> areas(static_cast<usize>(triangleCount), 0);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, soup.vertices.data(), vertexCount,
                                soup.triangles.data(), triangleCount, areas.data());
        if (!rcRasterizeTriangles(&context, soup.vertices.data(), vertexCount, soup.triangles.data(), areas.data(),
                                  triangleCount, *owned.heights, config.walkableClimb))
            return;
        rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *owned.heights);
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *owned.heights);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *owned.heights);

        owned.compact = rcAllocCompactHeightfield();
        if (owned.compact == nullptr ||
            !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *owned.heights,
                                       *owned.compact))
            return;
        if (!rcErodeWalkableArea(&context, config.walkableRadius, *owned.compact))
            return;
        if (!rcBuildDistanceField(&context, *owned.compact))
            return;
        if (!rcBuildRegions(&context, *owned.compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
            return;
        owned.contours = rcAllocContourSet();
        if (owned.contours == nullptr || !rcBuildContours(&context, *owned.compact, config.maxSimplificationError,
                                                          config.maxEdgeLen, *owned.contours))
            return;
        if (owned.contours->nconts == 0)
            return;
        owned.polys = rcAllocPolyMesh();
        if (owned.polys == nullptr || !rcBuildPolyMesh(&context, *owned.contours, config.maxVertsPerPoly, *owned.polys))
            return;
        owned.detail = rcAllocPolyMeshDetail();
        if (owned.detail == nullptr ||
            !rcBuildPolyMeshDetail(&context, *owned.polys, *owned.compact, config.detailSampleDist,
                                   config.detailSampleMaxError, *owned.detail))
            return;
        if (owned.polys->npolys == 0)
            return;

        for (int at = 0; at < owned.polys->npolys; ++at)
            owned.polys->flags[at] = owned.polys->areas[at] == RC_WALKABLE_AREA ? WalkableFlag : 0;

        dtNavMeshCreateParams params{};
        params.verts = owned.polys->verts;
        params.vertCount = owned.polys->nverts;
        params.polys = owned.polys->polys;
        params.polyAreas = owned.polys->areas;
        params.polyFlags = owned.polys->flags;
        params.polyCount = owned.polys->npolys;
        params.nvp = owned.polys->nvp;
        params.detailMeshes = owned.detail->meshes;
        params.detailVerts = owned.detail->verts;
        params.detailVertsCount = owned.detail->nverts;
        params.detailTris = owned.detail->tris;
        params.detailTriCount = owned.detail->ntris;
        params.walkableHeight = m_agent.height;
        params.walkableRadius = m_agent.radius;
        params.walkableClimb = m_agent.maxClimb;
        params.tileX = key.x;
        params.tileY = key.z;
        params.tileLayer = 0;
        std::memcpy(params.bmin, owned.polys->bmin, sizeof(params.bmin));
        std::memcpy(params.bmax, owned.polys->bmax, sizeof(params.bmax));
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;

        unsigned char* data = nullptr;
        int dataSize = 0;
        if (!dtCreateNavMeshData(&params, &data, &dataSize))
            return;
        dtTileRef ref = 0;
        if (dtStatusFailed(m_mesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, &ref))) {
            dtFree(data);
            return;
        }
        record.ref = ref;
    }

    const scene::World& m_world;
    core::InstanceId m_workspace;
    NavAgent m_agent;
    u64 m_tick = 0;
    bool m_gathered = false;
    u64 m_gatheredTick = 0;
    u64 m_gatheredMutations = 0;
    u64 m_gatheredTerrains = 0;
    std::vector<Static> m_statics;
    std::map<TileKey, TileRecord> m_tiles;
    dtNavMesh* m_mesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
    std::array<float, 3> m_extents{};
};

} // namespace

std::unique_ptr<INavigation> createNavigation(const scene::World& world)
{
    return std::make_unique<RecastNavigation>(world);
}

} // namespace luaug::nav
