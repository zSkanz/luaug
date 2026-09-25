# 0098 — Navigation: agent types, area costs, links, crowds and the plane

- Status: accepted
- Date: 2026-09-25
- Decided by: the agent, under the owner's mandate of 2026-09-24 ("do all of
  it", the depth the phases deferred) and his word of 2026-09-25 to build it
- Amends: ADR 0089 (one agent, tiles built where queries go). Everything that
  ADR decided stands; this adds to it.

## Context

ADR 0089 built one navigation mesh for one agent size, walkable or not, with
no way across a gap the mesh does not join and no way for many agents to walk
without walking through each other. The 2026-09-24 mandate lists the five
things a game reaches for next: more than one agent size, ground that costs
more or is forbidden, jumps and ladders, crowds, and paths on the 2D plane.

## How mature engines do it

- **Unity**: agent types, each with its own baked mesh; area types with a cost
  per area, set on volumes (`NavMeshModifierVolume`); `OffMeshLink` and
  `NavMeshLink` components between two points; `NavMeshAgent` with local
  avoidance. 2D has no built-in navigation.
- **Unreal**: supported agents, one mesh each; `NavArea` classes with a cost,
  applied by `NavModifierVolume`; `NavLinkProxy` for jumps; the crowd manager
  (DetourCrowd) for avoidance.
- **Godot**: one map per agent size; navigation layers and a travel cost per
  region; `NavigationLink3D`; `NavigationAgent3D` with RVO avoidance; a 2D
  navigation server with the same shape.

The same five ideas in each, and two of them, links and crowds, are Detour's own
features: the vendored tree already carries DetourCrowd and off-mesh
connections.

## Decision

1. **Agent types are named, and each has a mesh.** The service's four `Agent`
   properties stay the default agent. `NavigationService:DefineAgent(name,
   radius, height, maxClimb, maxSlope)` adds another, and every query takes an
   optional agent name. One `INavigation` per agent, built lazily like the
   default one; defining a name twice redefines it and drops its tiles.

2. **Areas are marked by instances and priced by the service.** A
   `NavigationArea` under a `BasePart` marks the ground inside that part's box
   with its `Label`. `NavigationService:SetAreaCost(label, cost)` prices a
   label for every agent: a multiplier on distance, and `math.huge` forbids it
   (a shut door). Unpriced labels cost 1. Recast's area ids carry the label;
   the query filter carries the costs, so re-pricing needs no rebuild.

3. **Links are instances too.** A `NavigationLink` joins two world points --
   `From` and `To` -- that the mesh does not, one way or both
   (`Bidirectional`), and carries a `Label` a script reads to know how to cross
   it ("Jump", "Ladder"). Detour's off-mesh connections, baked into the tile
   that holds the start. `FindPath` answers a third value: per waypoint, the
   label of the link that begins there, or `""` for walking.

4. **Crowds are agents the engine moves.** A `NavigationAgent` under a
   `BasePart` walks it to `Target` at up to `MaxSpeed`, avoiding the other
   agents, on the simulation tick (R10: DetourCrowd is single-threaded and fed
   in instance order; the part is moved like any other script write).
   `Reached` fires when it arrives. Over the vendored DetourCrowd, compiled now
   that something calls it -- the same pinned tree, not a new dependency.

5. **The plane has its own search.** A 2D world is a grid more often than a
   mesh: `NavigationService:FindPath2D(from, to)` searches the cells of every
   `Tilemap2D` (a non-empty, colliding tile is a wall) with anchored,
   colliding `Part2D`s stamped over them as walls, 8-connected without cutting
   corners, and answers `Vector2` waypoints with the straight runs merged. A*
   over the grid, deterministic by construction.

## Consequences

- A navigation mesh per agent type costs its own memory and build time, so a
  type is only built where queries for it go -- ADR 0089's rule, per agent.
- Area costs are global per label rather than per agent. A game wanting a
  per-agent price defines another label; per-agent filters are a later step if
  somebody needs one.
- Crowd agents write their parts' positions every tick: an anchored part that
  is also crowd-moved is the game's choice to make, and a physical one is
  moved like a script moving it.
- `FindPath`'s third return is additive: every existing caller reads two.
