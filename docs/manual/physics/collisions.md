# Collisions and contact

Two properties decide what a part does to everything else, and they are
deliberately independent.

| Property | Default | What it controls |
|---|---|---|
| `BasePart.CanCollide` | `true` | Whether this part physically stops anything. |
| `BasePart.CanQuery` | `true` | Whether raycasts, shapecasts and box queries can see it. |

They are separate because a part that blocks movement and a part a camera ray
should ignore are different questions. A glass pane blocks and should be seen; a
camera-collision volume blocks nothing and should be.

## A part that does not collide still notices

This is the sentence the whole page turns on:

> `CanCollide = false` still reports `BasePart.Touched`.

That is what makes a checkpoint, a trigger volume and a pickup work — things
pass through, and the part notices.

```luau
--!strict
local pad = Instance.new("Part")
pad.Size = vector.create(4, 0.4, 4)
pad.Position = vector.create(0, 0.2, -10)
pad.Anchored = true
pad.CanCollide = false        -- walk through it
pad.Parent = workspace

pad.Touched:Connect(function(other: BasePart)
    print(`{other.Name} stepped on the pad`)
end)
```

## Touched and TouchEnded

`BasePart.Touched` fires once when another part begins touching this one, with
the part it touched. `BasePart.TouchEnded` fires when a pair that was touching
stops.

Both are **deferred**, like every signal here, so they arrive at the next
resumption point rather than inside the solver. The contacts a tick produced are
delivered by that tick's `RunService.PostSimulation` drain rather than a frame
later.

Four rules that decide whether your handler runs:

- **A pair that stays in contact fires once, not once per tick** — including
  across the moment the simulation puts both parts to sleep.
- **Both parts fire.** `Touched` is a fact about each part, and a script
  connects to one of them without knowing which side of the pair it is.
- **Destroying a part does not fire `TouchEnded`** for its contacts. The
  instance is gone, and a signal on an instance nobody can reach is a signal
  nobody can handle.
- **Two anchored parts never touch each other.** Static against static is the
  pair the broad phase exists to skip: two things that never move cannot begin
  to overlap. A trigger volume built out of two anchored parts reports nothing.

That last one has a corollary worth holding on to: an anchored part whose
`CFrame` is being written becomes kinematic and *does* collide — so a moving
platform and a static wall do interact, while two static walls do not.

## Surfaces

`BasePart.Friction` defaults to 0.3 and `BasePart.Restitution` to 0. Both are
combined across a contact pair rather than taken from one side, so one slippery
surface is enough to make a pair slide and one bouncy surface is enough to make
a pair bounce.

Neither is a material. There is no `BasePart.Material` in this release — a
surface look is a different thing from rigid-body state, and a property nothing
reads would look more like a working API than a missing one does.

## Collision groups

A collision group is a name. Parts carry one, and you say which pairs of names
collide.

```luau
--!strict
local PhysicsService = game:GetService("PhysicsService")

PhysicsService:RegisterCollisionGroup("Player")
PhysicsService:RegisterCollisionGroup("Debris")
PhysicsService:CollisionGroupSetCollidable("Player", "Debris", false)

crate.CollisionGroup = "Debris"
```

The four rules:

1. **A new group collides with everything until told otherwise.**
2. **Registering a name that already exists is a no-op**, not an error — which
   is what lets a script register its groups at file scope and survive a reload.
3. **`PhysicsService.CollisionGroupSetCollidable` is symmetric.** Setting `a`
   against `b` sets `b` against `a`, because a one-way collision is not a thing
   a solver can express.
4. **Assigning an unregistered name raises** rather than falling back to
   `Default`. The failure mode of a typo here is a wall players walk through,
   which is expensive to find and cheap to refuse.

`PhysicsService.GetRegisteredCollisionGroups` returns every registered name in
registration order with `Default` first — a fresh array on every call, and
ordered rather than hashed so that iterating it cannot leak a container's own
order into a simulation.

A world holds at most 1024 groups.

Collision groups also decide **character against character**: what holds the
block is the rigid body inside the capsule, so two characters in a group set
never to collide with itself walk straight through one another.

## Where to look next

- [Rigid bodies](manual:physics/bodies) — what a body is in the first place
- [Raycasts and queries](manual:physics/queries) — the other half of `CanQuery`
- [`PhysicsService`](api:PhysicsService)
