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
