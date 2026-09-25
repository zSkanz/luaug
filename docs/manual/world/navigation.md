# Navigation

`NavigationService` answers where a character can walk and how it gets
somewhere. The walkable ground is every **anchored, colliding** part under
`Workspace` and the `Terrain`'s surface. From that ground the service builds a
navigation mesh for one agent size. You do not bake or place anything: the mesh
is built where your queries go.

`examples/21-navigation` is a walker finding its way through a maze.

## Asking for a path

```luau
--!strict
local NavigationService = game:GetService("NavigationService")

local points, complete = NavigationService:FindPath(vector.create(0, 0, 0), vector.create(40, 0, 12))
if points == nil then
    print("not standing on walkable ground")
elseif not complete then
    print("the goal is cut off, or past ground not built yet: walk as far as this goes")
end
```

- **The answer is corners.** Walk to each point in turn in a straight line. The
  first point is where you start, and the last is the goal, or as near to it as
  the ground allows.
- **Nil means there is no path from here**: the start is not on walkable ground.
  A path that stops short still comes back, with `complete` false. Walk it and
  ask again from where it ends.
- **A query sees the world as it is now.** Build a wall and ask in the same
  breath, and the path goes round the wall. A door that opened is walkable on
  the next query.

## Following it

Following a path is game code, because every game wants it to feel different.
The whole of it is a `CharacterBody` walking towards the next corner:

```luau
--!strict
local RunService = game:GetService("RunService")

local walker = Instance.new("CharacterBody")
walker.Parent = workspace
local waypoints: { vector } = {}
local nextPoint = 2

RunService.PreSimulation:Connect(function()
    if nextPoint > #waypoints then
        walker:Move(vector.zero)
        return
    end
    local target = waypoints[nextPoint]
    local offset = vector.create(target.x - walker.Position.x, 0, target.z - walker.Position.z)
    if vector.magnitude(offset) < 0.35 then
        nextPoint += 1
    else
        walker:Move(vector.normalize(offset))
    end
end)
```

## The agent

Four properties describe the one body the mesh is built for:

| Property | Default | What it decides |
|---|---|---|
| `AgentRadius` | 0.5 | How far paths keep from walls |
| `AgentHeight` | 2 | How low a gap it will not fit under |
| `AgentMaxClimb` | 0.5 | The highest step it walks up rather than around |
| `AgentMaxSlope` | 45 | The steepest ground it walks on, in degrees |

Match them to your `CharacterBody` (`Size`, `AutoStepHeight`, `MaxSlopeAngle`).
An agent that plans a path its own body cannot walk is worse than one that
finds none. Changing any of them rebuilds the mesh from nothing.

## The two cheap questions

- **`NearestPoint(point, maxDistance?)`** is the nearest walkable point: where a
  spawn, a teleport or a click on the world lands before it asks for a path.
- **`Raycast(from, to)`** walks straight towards `to` and answers where it
  stopped. Most steps of most paths are straight, so ask this before
  `FindPath`.

## Building ahead

The mesh is built in square tiles, only where queries reach, so the first path
through new ground pays for building it. `BuildRegion(minimum, maximum)` pays
up front, for an arena as it loads or a level behind a loading screen. Ground
already built and unchanged costs nothing to ask for again.

## More than one body

The four properties above are the service's own agent. A game with a giant
and a rat defines the others by name, and each gets a mesh of its own:

```luau
Navigation:DefineAgent("Giant", 1.5, 4)          -- radius, height
Navigation:DefineAgent("Rat", 0.2, 0.3, 0.2, 60) -- ..., maxClimb, maxSlope
local path, reached = Navigation:FindPath(from, to, "Giant")
```

A name nobody defined has no ground: `FindPath` answers `nil`. Defining a name
again redefines it and drops the mesh it had.

## Ground that costs more

A `NavigationArea` under a part labels the ground inside the part's box. The
part need not collide -- a pool of water is a part with `CanCollide` off and a
`NavigationArea` labelled `"Water"` inside it. The service prices each label
for every agent, as a multiplier on distance:

```luau
Navigation:SetAreaCost("Water", 10)       -- walked round when round is shorter than ten times across
Navigation:SetAreaCost("Door", math.huge) -- never walked
Navigation:SetAreaCost("Door", 1)         -- open again; nothing is rebuilt
```

A label nobody priced costs 1, the same as any other ground. Prices are read
at query time, so opening a door is a price, not a rebuild. Moving or resizing
the part is a rebuild of the tiles it touches.

## Gaps the mesh does not cross

A `NavigationLink` joins two points: a jump between roofs, a ladder, a
teleporter. `From` and `To` are absolute; `Bidirectional` (on by default) lets
the link be taken both ways. A path that uses it says so in its third answer,
one label per waypoint -- the link's `Label` at the waypoint where it begins,
`""` where the way on is walking:

```luau
local path, reached, labels = Navigation:FindPath(from, to)
for index, point in path do
    if labels[index] == "Jump" then
        -- the next waypoint is across the gap: jump to it
    end
end
```

## Crowds

A `NavigationAgent` under a part walks the part itself. Set its `Target` and it
goes, at `MaxSpeed` metres per second, over the mesh its `AgentType` names
(empty is the service's own), steering round every other agent on the way. It
moves on the fixed tick, the same on every run, and fires `Reached` when it
arrives -- `Active` goes false and the part stops.

```luau
local agent = Instance.new("NavigationAgent")
agent.MaxSpeed = 6
agent.Parent = guard
agent.Target = vector.create(20, 0, 5)
agent.Reached:Connect(function()
    agent.Target = vector.create(-20, 0, 5)
end)
```

A crowd agent moves the part's `CFrame`; it does not push a `CharacterBody`.
For a body with physics, walk a `FindPath` yourself as above.

## On the plane

A 2D game asks `FindPath2D(from, to)` with two `Vector2`s. There is no mesh:
the search runs over cells -- the first `Tilemap2D`'s size and alignment, or a
metre when there is no tilemap -- and every colliding tile and every anchored,
colliding `Part2D` is a wall. It answers the points where the path turns and
whether it reaches the goal, and `nil` when `from` is inside a wall. Paths go
round a wall's corner, never across it.
