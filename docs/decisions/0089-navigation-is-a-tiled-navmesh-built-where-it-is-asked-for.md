# 0089 — Navigation is a tiled navmesh, built where it is asked for

- Status: accepted
- Date: 2026-09-24
- Decided by: the agent, under the owner's mandate of 2026-09-23 (phase 3
  opened: "2D and navmesh")
- Amends: ADR 0022 (Recast/Detour vendored with the seam only). This is the
  integration it deferred.

## Context

ADR 0022 vendored Recast/Detour 1.6.0 and wrote `engine/nav/nav.h`, a seam
that nothing implemented. The seam made one real commitment: tiles, because a
world larger than memory cannot hold one navmesh. It left the rest open on
purpose: the generation pipeline, the agent model, and dirtiness.

## How mature engines do it

- **Unity** bakes offline into a NavMesh asset per agent type. `NavMeshSurface`
  can rebuild at runtime, whole or asynchronously, and a `NavMeshAgent` walks
  it.
- **Unreal** builds Recast tiles inside `NavMeshBoundsVolume`s. It can be
  static, rebuilt dynamically as geometry changes (dirty tiles), or built only
  around "navigation invokers" near the player. The last mode is the one for
  open worlds.
- **Godot** bakes a `NavigationRegion3D` from parsed geometry, and a server
  merges regions and answers queries.

All three use one mesh per agent size, all three tile, and the open-world
answer is to build tiles near where they are needed.

## Decision

1. **A `NavigationService` builds Recast tiles where queries go.** A query
   builds, or rebuilds if dirty, every tile in the rectangle its two ends span
   plus a margin of one tile. Up to a fixed budget, and in tile order, so the
   work is a function of the query sequence (R10). A path through a region no
   query has reached comes back **partial**. A game that wants a region ready
   before its first path asks `NavigationService:BuildRegion`. Nothing is
   baked offline. The world is data a script and the editor both change, and
   an asset that goes stale the moment a wall moves is the failure this
   avoids.
2. **One agent per service**: `AgentRadius`, `AgentHeight`, `AgentMaxClimb`
   and `AgentMaxSlope` are service properties, and changing one invalidates
   every tile. A mesh is eroded by the agent's radius, so a per-query agent
   means a mesh per size. That is the second agent type every engine above
   eventually adds, and it is not needed to ship the first.
3. **What is walkable is what is static and solid**: anchored, colliding
   `BasePart`s under `Workspace` and `Terrain`'s ground. A part moving under
   physics, a character and a `Part2D` are not the ground. Parts are
   rasterised by their shape's box, the same approximation the collider uses
   for a wedge. Terrain comes from the same mesher the renderer and collider
   use.
4. **Dirtiness is a fingerprint per tile**: a hash of the static geometry
   overlapping it, recomputed at most once per tick and only for tiles a query
   touches. A wall that moved re-fingerprints its tiles, and the next query
   through them rebuilds them. No change events and no subscriptions: nothing
   can forget to send one.
5. **The script sees paths as waypoints**:
   `NavigationService:FindPath(from, to)` answers the waypoint array and
   whether it reaches the goal, or nil when there is no path at all. That
   distinction is the one `nav.h` insisted on. `NearestPoint` and `Raycast`
   (a straight walk that stops where the mesh does) are the two cheap
   questions. Following a path is game code, as a platformer's controller is:
   a `CharacterBody` walks towards the next waypoint.
6. **Coordinates**: Recast works in f32 world space. At a hundred kilometres
   from the origin an f32 metre has eight-millimetre steps, well inside a
   navmesh cell. Waypoints are handed to scripts as `vector`s, as every
   position is.

## Consequences

- `engine/nav` becomes a real module (L4). It reads `scene` and `asset` for
  geometry and links Recast/Detour privately, so no Recast type is in its
  header (R17). `script` reaches it through the `INavigation` seam, as it
  reaches physics.
- A first path through new ground costs a tile build on that tick: a few
  milliseconds a tile at the default cell size. Budgeted per query.
- Not in this decision: crowds and local avoidance (DetourCrowd is vendored),
  off-mesh links, area costs, and 2D navigation. Each is an addition beside
  this one.
