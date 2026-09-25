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

#include <DetourCrowd.h>
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
#include <string>
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
// Area ids for `NavigationArea` labels (ADR 0098): one to 62, Recast's own
// walkable area being 63. The first fifteen also get a flag bit each, which is
// how a label priced at infinity is excluded outright rather than merely dear.
constexpr unsigned char MostAreaLabels = 62;
constexpr unsigned char ExcludableAreas = 15;
constexpr int MaxCrowdAgents = 256;

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

// **What every agent type shares** (ADR 0098): the area labels, in the order
// they were first seen -- an id is its place plus one, so it never changes
// while the process runs -- their prices, and each link's label by the index
// of the instance that holds it.
struct Shared
{
    std::vector<std::string> labels;
    std::map<std::string, f32, std::less<>> costs;
    std::map<core::u32, std::string> linkLabels;

    [[nodiscard]] unsigned char areaOf(std::string_view label)
    {
        for (usize at = 0; at < labels.size(); ++at) {
            if (labels[at] == label)
                return static_cast<unsigned char>(at + 1);
        }
        // Past the last id, labels share it: counted rather than refused, a
        // world with sixty-three kinds of mud is one nobody will build.
        if (labels.size() >= MostAreaLabels)
            return MostAreaLabels;
        labels.emplace_back(label);
        return static_cast<unsigned char>(labels.size());
    }

    // The query filter every search uses: walkable ground, each label at its
    // price, and a label priced at infinity left out.
    [[nodiscard]] dtQueryFilter filter() const
    {
        dtQueryFilter out;
        out.setIncludeFlags(WalkableFlag);
        unsigned short exclude = 0;
        for (usize at = 0; at < labels.size(); ++at) {
            const auto id = static_cast<int>(at + 1);
            const auto found = costs.find(labels[at]);
            const f32 cost = found != costs.end() ? found->second : 1.0f;
            if (!std::isfinite(cost)) {
                if (id <= ExcludableAreas)
                    exclude = static_cast<unsigned short>(exclude | (1u << id));
                else
                    out.setAreaCost(id, 1.0e6f);
                continue;
            }
            out.setAreaCost(id, std::max(cost, 0.001f));
        }
        out.setExcludeFlags(exclude);
        return out;
    }
};

// A `NavigationArea`'s box, gathered with the statics.
struct AreaBox
{
    f64 minX = 0.0;
    f64 minY = 0.0;
    f64 minZ = 0.0;
    f64 maxX = 0.0;
    f64 maxY = 0.0;
    f64 maxZ = 0.0;
    std::string label;
    u64 hash = 0;
};

// A `NavigationLink`, gathered with the statics.
struct LinkRecord
{
    core::InstanceId id;
    DVec3 from{};
    DVec3 to{};
    bool bidirectional = true;
    u64 hash = 0;
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

// **One agent type's walkable ground**: a mesh, its query, its tiles and its
// crowd. `RecastNavigation` below holds one of these per type (ADR 0098).
class AgentMesh
{
public:
    AgentMesh(const scene::World& world, Shared& shared) : m_world(world), m_shared(shared) {}

    ~AgentMesh()
    {
        if (m_crowd != nullptr)
            dtFreeCrowd(m_crowd);
        if (m_query != nullptr)
            dtFreeNavMeshQuery(m_query);
        if (m_mesh != nullptr)
            dtFreeNavMesh(m_mesh);
    }

    AgentMesh(const AgentMesh&) = delete;
    AgentMesh& operator=(const AgentMesh&) = delete;

    void setWorkspace(core::InstanceId workspace) noexcept
    {
        if (workspace != m_workspace) {
            m_workspace = workspace;
            m_gathered = false;
        }
    }

    void setAgent(const NavAgent& agent)
    {
        if (agent == m_agent)
            return;
        m_agent = agent;
        invalidate();
    }

    void setTick(u64 tick) noexcept { m_tick = tick; }

    [[nodiscard]] std::optional<NavPath> findPath(DVec3 from, DVec3 to)
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
        std::array<unsigned char, MaxStraightPoints> flags{};
        std::array<dtPolyRef, MaxStraightPoints> refs{};
        int points = 0;
        m_query->findStraightPath(start, target, polys.data(), count, straight.data(), flags.data(), refs.data(),
                                  &points, MaxStraightPoints, 0);
        path.points.reserve(static_cast<usize>(points));
        path.labels.reserve(static_cast<usize>(points));
        for (int at = 0; at < points; ++at) {
            path.points.push_back(pointOf(&straight[static_cast<usize>(at) * 3]));
            // **Where a link begins, its label** (ADR 0098): the script
            // walking the path jumps there instead of walking.
            std::string label;
            if ((flags[static_cast<usize>(at)] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0) {
                if (const dtOffMeshConnection* link = m_mesh->getOffMeshConnectionByRef(refs[static_cast<usize>(at)]);
                    link != nullptr) {
                    if (const auto found = m_shared.linkLabels.find(link->userId); found != m_shared.linkLabels.end())
                        label = found->second;
                }
            }
            path.labels.push_back(std::move(label));
        }
        path.complete = reached && goalOnMesh;
        return path;
    }

    [[nodiscard]] std::optional<DVec3> nearestPoint(DVec3 point, f32 maxDistance)
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

    [[nodiscard]] std::optional<DVec3> raycast(DVec3 from, DVec3 to)
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

    usize buildRegion(DVec3 minimum, DVec3 maximum)
    {
        prepare();
        return ensureTiles(std::min(minimum.x, maximum.x), std::min(minimum.z, maximum.z),
                           std::max(minimum.x, maximum.x), std::max(minimum.z, maximum.z), 0.0, RegionTileBudget);
    }

    void invalidate()
    {
        // The crowd holds references into the mesh, so it goes first.
        if (m_crowd != nullptr)
            dtFreeCrowd(m_crowd);
        m_crowd = nullptr;
        m_slots.clear();
        if (m_query != nullptr)
            dtFreeNavMeshQuery(m_query);
        if (m_mesh != nullptr)
            dtFreeNavMesh(m_mesh);
        m_query = nullptr;
        m_mesh = nullptr;
        m_tiles.clear();
    }

    [[nodiscard]] usize tileCount() const noexcept
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

    [[nodiscard]] dtQueryFilter walkable() const { return m_shared.filter(); }

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

        // **The labelled boxes** (ADR 0098), gathered in instance order so a
        // label's area id is a function of the world.
        m_areas.clear();
        m_world.navigationAreas().forEach([&](core::InstanceId id, const scene::NavigationAreaComponent& area) {
            const core::InstanceId holder = m_world.parentOf(id);
            const scene::PartComponent* part = holder.valid() ? m_world.parts().find(holder) : nullptr;
            if (part == nullptr || !inWorld(m_world, m_workspace, holder))
                return;
            AreaBox box;
            box.minX = box.minY = box.minZ = std::numeric_limits<f64>::max();
            box.maxX = box.maxY = box.maxZ = std::numeric_limits<f64>::lowest();
            for (const DVec3& corner : cornersOf(part->cframe, part->size)) {
                box.minX = std::min(box.minX, corner.x);
                box.minY = std::min(box.minY, corner.y);
                box.minZ = std::min(box.minZ, corner.z);
                box.maxX = std::max(box.maxX, corner.x);
                box.maxY = std::max(box.maxY, corner.y);
                box.maxZ = std::max(box.maxZ, corner.z);
            }
            box.label = area.label;
            (void)m_shared.areaOf(area.label);
            Fingerprint hash;
            hash.add(box.minX);
            hash.add(box.minY);
            hash.add(box.minZ);
            hash.add(box.maxX);
            hash.add(box.maxY);
            hash.add(box.maxZ);
            for (const char c : area.label)
                hash.add(c);
            box.hash = hash.value;
            m_areas.push_back(std::move(box));
        });
        // **The links** (ADR 0098), each baked into the tile its start is in.
        m_links.clear();
        m_world.navigationLinks().forEach([&](core::InstanceId id, const scene::NavigationLinkComponent& link) {
            if (!inWorld(m_world, m_workspace, id))
                return;
            LinkRecord record{id, link.from, link.to, link.bidirectional, 0};
            m_shared.linkLabels[id.index] = link.label;
            Fingerprint hash;
            hash.add(id.index);
            hash.add(link.from);
            hash.add(link.to);
            hash.add(link.bidirectional);
            record.hash = hash.value;
            m_links.push_back(record);
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

    // Whether a link's start is inside a tile -- the tile it is baked into.
    [[nodiscard]] bool linkInTile(const LinkRecord& link, TileKey key) const noexcept
    {
        const f64 size = tileMetres();
        return static_cast<i32>(std::floor(link.from.x / size)) == key.x &&
               static_cast<i32>(std::floor(link.from.z / size)) == key.z;
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
        for (const AreaBox& area : m_areas) {
            if (area.maxX < minX || area.minX > maxX || area.maxZ < minZ || area.minZ > maxZ)
                continue;
            hash.add(area.hash);
        }
        for (const LinkRecord& link : m_links) {
            if (linkInTile(link, key))
                hash.add(link.hash);
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
        // **Each labelled box marks the ground inside it** (ADR 0098), after
        // the erosion as Recast's own sample does, so the label reaches the
        // edge an agent can actually stand on. The box is the part's, as the
        // world sees it square: a turned part marks what it spans.
        for (const AreaBox& area : m_areas) {
            const float lowest[3] = {static_cast<float>(area.minX), static_cast<float>(area.minY),
                                     static_cast<float>(area.minZ)};
            const float highest[3] = {static_cast<float>(area.maxX), static_cast<float>(area.maxY),
                                      static_cast<float>(area.maxZ)};
            rcMarkBoxArea(&context, lowest, highest, m_shared.areaOf(area.label), *owned.compact);
        }
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

        // Walkable wherever an area was kept, and one flag bit more for the
        // first labels, so the filter can leave a forbidden one out.
        for (int at = 0; at < owned.polys->npolys; ++at) {
            const unsigned char area = owned.polys->areas[at];
            unsigned short flags = area == RC_NULL_AREA ? 0 : WalkableFlag;
            if (area >= 1 && area <= ExcludableAreas)
                flags = static_cast<unsigned short>(flags | (1u << area));
            owned.polys->flags[at] = flags;
        }

        // **The links that start here** (ADR 0098), as Detour's off-mesh
        // connections: this tile holds them, and Detour joins their far ends
        // to whichever tile holds that ground when it is added.
        std::vector<float> linkVerts;
        std::vector<float> linkRadii;
        std::vector<unsigned char> linkDirections;
        std::vector<unsigned char> linkAreas;
        std::vector<unsigned short> linkFlags;
        std::vector<unsigned int> linkIds;
        for (const LinkRecord& link : m_links) {
            if (!linkInTile(link, key))
                continue;
            for (const DVec3& end : {link.from, link.to}) {
                linkVerts.push_back(static_cast<float>(end.x));
                linkVerts.push_back(static_cast<float>(end.y));
                linkVerts.push_back(static_cast<float>(end.z));
            }
            linkRadii.push_back(m_agent.radius);
            linkDirections.push_back(link.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
            linkAreas.push_back(RC_WALKABLE_AREA);
            linkFlags.push_back(WalkableFlag);
            linkIds.push_back(link.id.index);
        }

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
        if (!linkIds.empty()) {
            params.offMeshConVerts = linkVerts.data();
            params.offMeshConRad = linkRadii.data();
            params.offMeshConDir = linkDirections.data();
            params.offMeshConAreas = linkAreas.data();
            params.offMeshConFlags = linkFlags.data();
            params.offMeshConUserID = linkIds.data();
            params.offMeshConCount = static_cast<int>(linkIds.size());
        }

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

public:
    // **One step of this type's crowd** (ADR 0098). `agents` are this type's,
    // in the order the host gathered them; an agent missing from them is
    // removed from the crowd.
    void stepCrowd(std::span<const CrowdAgentState> agents, f32 dt, std::vector<CrowdAgentStep>& out)
    {
        prepare();
        for (const CrowdAgentState& agent : agents) {
            ensureTiles(std::min(agent.position.x, agent.target.x), std::min(agent.position.z, agent.target.z),
                        std::max(agent.position.x, agent.target.x), std::max(agent.position.z, agent.target.z),
                        tileMetres(), QueryTileBudget);
        }
        const auto unchanged = [&out](const CrowdAgentState& agent) {
            out.push_back(CrowdAgentStep{agent.id, agent.position, DVec3{}, false});
        };
        if (!ready()) {
            for (const CrowdAgentState& agent : agents)
                unchanged(agent);
            return;
        }
        if (m_crowd == nullptr) {
            m_crowd = dtAllocCrowd();
            if (m_crowd == nullptr || !m_crowd->init(MaxCrowdAgents, std::max(m_agent.radius, 0.1f), m_mesh)) {
                if (m_crowd != nullptr)
                    dtFreeCrowd(m_crowd);
                m_crowd = nullptr;
                for (const CrowdAgentState& agent : agents)
                    unchanged(agent);
                return;
            }
        }
        // The prices may have changed since the last step.
        *m_crowd->getEditableFilter(0) = walkable();

        // Gone from the world, gone from the crowd.
        std::erase_if(m_slots, [&](const Slot& slot) {
            const bool present = std::any_of(agents.begin(), agents.end(),
                                             [&slot](const CrowdAgentState& agent) { return agent.id == slot.id; });
            if (!present)
                m_crowd->removeAgent(slot.index);
            return !present;
        });

        std::vector<int> indices;
        indices.reserve(agents.size());
        for (const CrowdAgentState& agent : agents) {
            auto slot =
                std::find_if(m_slots.begin(), m_slots.end(), [&agent](const Slot& s) { return s.id == agent.id; });
            dtCrowdAgentParams params{};
            params.radius = m_agent.radius;
            params.height = m_agent.height;
            params.maxSpeed = agent.maxSpeed;
            params.maxAcceleration = agent.maxSpeed * 4.0f;
            params.collisionQueryRange = m_agent.radius * 12.0f;
            params.pathOptimizationRange = m_agent.radius * 30.0f;
            params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO |
                                 DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
            params.obstacleAvoidanceType = 3;
            params.separationWeight = 2.0f;
            params.queryFilterType = 0;
            if (slot == m_slots.end()) {
                const float at[3] = {static_cast<float>(agent.position.x), static_cast<float>(agent.position.y),
                                     static_cast<float>(agent.position.z)};
                const int index = m_crowd->addAgent(at, &params);
                if (index < 0) {
                    indices.push_back(-1);
                    continue;
                }
                m_slots.push_back(Slot{agent.id, index, DVec3{}, false});
                slot = std::prev(m_slots.end());
            }
            else {
                m_crowd->updateAgentParameters(slot->index, &params);
            }
            // A new target, or walking again: ask the crowd for the way.
            const bool retarget =
                agent.active && (!slot->active || slot->target.x != agent.target.x ||
                                 slot->target.y != agent.target.y || slot->target.z != agent.target.z);
            if (retarget) {
                const dtQueryFilter filter = walkable();
                dtPolyRef ref = 0;
                float goal[3];
                if (nearest(agent.target, searchExtents(), filter, ref, goal))
                    m_crowd->requestMoveTarget(slot->index, ref, goal);
            }
            else if (!agent.active && slot->active) {
                m_crowd->resetMoveTarget(slot->index);
            }
            slot->target = agent.target;
            slot->active = agent.active;
            indices.push_back(slot->index);
        }

        m_crowd->update(dt, nullptr);

        for (usize at = 0; at < agents.size(); ++at) {
            const CrowdAgentState& agent = agents[at];
            const dtCrowdAgent* state = indices[at] >= 0 ? m_crowd->getAgent(indices[at]) : nullptr;
            if (state == nullptr || !state->active) {
                unchanged(agent);
                continue;
            }
            CrowdAgentStep step;
            step.id = agent.id;
            step.position = pointOf(state->npos);
            step.velocity = pointOf(state->vel);
            // Arrived: within its own radius of the goal, on the ground.
            const f64 dx = step.position.x - agent.target.x;
            const f64 dz = step.position.z - agent.target.z;
            const f64 reach = static_cast<f64>(m_agent.radius) + 0.1;
            step.reached = agent.active && dx * dx + dz * dz <= reach * reach;
            out.push_back(step);
        }
    }

private:
    struct Slot
    {
        core::InstanceId id;
        int index = -1;
        DVec3 target{};
        bool active = false;
    };

    const scene::World& m_world;
    Shared& m_shared;
    core::InstanceId m_workspace;
    NavAgent m_agent;
    u64 m_tick = 0;
    bool m_gathered = false;
    u64 m_gatheredTick = 0;
    u64 m_gatheredMutations = 0;
    u64 m_gatheredTerrains = 0;
    std::vector<Static> m_statics;
    std::vector<AreaBox> m_areas;
    std::vector<LinkRecord> m_links;
    std::map<TileKey, TileRecord> m_tiles;
    dtNavMesh* m_mesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
    dtCrowd* m_crowd = nullptr;
    std::vector<Slot> m_slots;
    std::array<float, 3> m_extents{};
};

// **Every agent type's ground, and what they share** (ADR 0098): the service's
// own agent, the ones `defineAgent` named, the area prices, and the link labels.
class RecastNavigation final : public INavigation
{
public:
    explicit RecastNavigation(const scene::World& world) : m_world(world), m_default(world, m_shared) {}

    void setWorkspace(core::InstanceId workspace) noexcept override
    {
        m_workspace = workspace;
        m_default.setWorkspace(workspace);
        for (auto& [name, mesh] : m_named)
            mesh->setWorkspace(workspace);
    }

    void setAgent(const NavAgent& agent) override { m_default.setAgent(agent); }

    void setTick(u64 tick) noexcept override
    {
        m_tick = tick;
        m_default.setTick(tick);
        for (auto& [name, mesh] : m_named)
            mesh->setTick(tick);
    }

    void defineAgent(std::string_view name, const NavAgent& agent) override
    {
        if (name.empty()) {
            m_default.setAgent(agent);
            return;
        }
        auto found = m_named.find(name);
        if (found == m_named.end()) {
            found = m_named.emplace(std::string(name), std::make_unique<AgentMesh>(m_world, m_shared)).first;
            found->second->setWorkspace(m_workspace);
            found->second->setTick(m_tick);
        }
        found->second->setAgent(agent);
    }

    void setAreaCost(std::string_view label, f32 cost) override
    {
        m_shared.costs[std::string(label)] = cost;
        (void)m_shared.areaOf(label);
    }

    [[nodiscard]] std::optional<NavPath> findPath(DVec3 from, DVec3 to, std::string_view agent) override
    {
        AgentMesh* mesh = meshFor(agent);
        return mesh != nullptr ? mesh->findPath(from, to) : std::nullopt;
    }

    [[nodiscard]] std::optional<DVec3> nearestPoint(DVec3 point, f32 maxDistance, std::string_view agent) override
    {
        AgentMesh* mesh = meshFor(agent);
        return mesh != nullptr ? mesh->nearestPoint(point, maxDistance) : std::nullopt;
    }

    [[nodiscard]] std::optional<DVec3> raycast(DVec3 from, DVec3 to, std::string_view agent) override
    {
        AgentMesh* mesh = meshFor(agent);
        return mesh != nullptr ? mesh->raycast(from, to) : std::nullopt;
    }

    usize buildRegion(DVec3 minimum, DVec3 maximum, std::string_view agent) override
    {
        AgentMesh* mesh = meshFor(agent);
        return mesh != nullptr ? mesh->buildRegion(minimum, maximum) : 0;
    }

    void stepCrowd(std::span<const CrowdAgentState> agents, f32 dt, std::vector<CrowdAgentStep>& out) override
    {
        out.clear();
        // Each type's agents in the order given, the service's own first and
        // then the named ones by name: an order the world decides.
        std::vector<CrowdAgentState> group;
        const auto step = [&](std::string_view type, AgentMesh& mesh) {
            group.clear();
            for (const CrowdAgentState& agent : agents) {
                if (agent.agentType == type)
                    group.push_back(agent);
            }
            mesh.stepCrowd(group, dt, out);
        };
        step({}, m_default);
        for (auto& [name, mesh] : m_named)
            step(name, *mesh);
        // An agent of a type nobody defined stands where it is.
        for (const CrowdAgentState& agent : agents) {
            if (!agent.agentType.empty() && m_named.find(agent.agentType) == m_named.end())
                out.push_back(CrowdAgentStep{agent.id, agent.position, DVec3{}, false});
        }
    }

    [[nodiscard]] std::optional<NavPath2D> findPath2D(core::Vec2 from, core::Vec2 to) override
    {
        return nav::findPath2D(m_world, m_workspace, from, to);
    }

    void invalidate() override
    {
        m_default.invalidate();
        for (auto& [name, mesh] : m_named)
            mesh->invalidate();
    }

    [[nodiscard]] usize tileCount() const noexcept override
    {
        usize count = m_default.tileCount();
        for (const auto& [name, mesh] : m_named)
            count += mesh->tileCount();
        return count;
    }

private:
    [[nodiscard]] AgentMesh* meshFor(std::string_view name)
    {
        if (name.empty())
            return &m_default;
        const auto found = m_named.find(name);
        return found != m_named.end() ? found->second.get() : nullptr;
    }

    const scene::World& m_world;
    Shared m_shared;
    AgentMesh m_default;
    std::map<std::string, std::unique_ptr<AgentMesh>, std::less<>> m_named;
    core::InstanceId m_workspace;
    u64 m_tick = 0;
};

} // namespace

std::unique_ptr<INavigation> createNavigation(const scene::World& world)
{
    return std::make_unique<RecastNavigation>(world);
}

} // namespace luaug::nav
