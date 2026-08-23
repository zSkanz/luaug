# CharacterBody

A capsule that walks. The player, or anything that should climb a ramp and step
over a kerb instead of tumbling.

`CharacterBody` extends `BasePart`, so it has a `CFrame`, a `Size`, a `Color`, a
`CollisionGroup` and everything else a part has. What it adds is that it is a
**controller rather than a rigid body**: it sweeps its own shape and moves at
the velocity you give it, which is why it does not tip over.

There is no `Humanoid` and no `HumanoidRootPart`. This is one instance.

## Making one

```luau
--!strict
local character = Instance.new("CharacterBody")
character.Size = vector.create(2, 5, 2)   -- diameter 2 m, full height 5 m
character.WalkSpeed = 7                   -- metres per second
character.JumpSpeed = 5.5
character.AutoStepHeight = 0.6
character.MaxSlopeAngle = 46
character.Position = vector.create(0, 6, 0)
character.Parent = workspace
```

`Size` becomes a capsule: **full height from `Size.y`, diameter from the larger
of `Size.x` and `Size.z`** — authored the way `BasePart.Size` is, so a 2 × 5 × 2
part and a character of the same size occupy the same volume.

`Position` is the capsule's **centre**, like any other part. Not its feet.

## Walking

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.Heartbeat:Connect(function(dt: number)
    character:Move(moveDirection)
end)
```

`CharacterBody.Move` sets the direction to walk in **for the next simulation
tick**, in world space. Four things about it:

- **Only the horizontal part is used.** Vertical movement is gravity's and
  `Jump`'s.
- **The vector is scaled by `WalkSpeed`, not normalised** — so a shorter one
  walks slower, which is exactly what a half-deflected thumbstick should do.
- **Call it every tick while moving.** A character told nothing stops.
- It is intent, not a teleport: the controller still sweeps, still collides, and
  still refuses to walk through a wall.

**Gravity is applied for you**, from `Workspace.Gravity`, integrated into the
character's own vertical velocity every tick. You do not add it to `Move`.

## Jumping

```luau
if character.Grounded then
    character:Jump()
end
```

`CharacterBody.Jump` launches the character upward at `CharacterBody.JumpSpeed`
at the next simulation tick, **wherever it is**. It does not check
`CharacterBody.Grounded`, and that is deliberate: `LinearVelocity` is read-only,
so a check inside `Jump` would make a double jump, a wall jump and a triple jump
impossible to write at all.

The line above is the old behaviour, in one line, in the game — where a jump
policy belongs. Coyote time and jump buffering become counters beside it.

Calling `Jump` every frame is flying. That is the game's bug to fix, not the
engine's to prevent.

How high a jump reaches depends on `Workspace.Gravity`. That relationship is
what a game tunes, rather than tuning a height.

## Standing on things

| Member | Type | Means |
|---|---|---|
| `CharacterBody.Grounded` | `boolean`, read-only | Standing on something walkable as of the last tick. |
| `CharacterBody.State` | `Enum.CharacterState`, read-only | The same fact as `Grounded` or `Airborne`. |
| `CharacterBody.Landed` | `Signal<BasePart?>` | Fired on becoming grounded after being airborne. |

`Enum.CharacterState` has two items and not three: ground too steep to walk on
reads as `Airborne`, because the question a script asks this property is whether
it may jump.

```luau
character.Landed:Connect(function(groundPart: BasePart?)
    -- nil when what it landed on is not an instance
end)
```

`Landed` is a **transition**, not a state — airborne last tick and grounded now.
Reading the flag alone would fire it every tick a character stands still.

## The two numbers that make it a character

`CharacterBody.MaxSlopeAngle` is the steepest ground, in degrees from
horizontal, the character can stand and walk on. Anything steeper is a wall it
slides down rather than a floor it stands on. It accepts 0 up to but not
including 90.

`CharacterBody.AutoStepHeight` is the tallest ledge, in metres, the character
walks over rather than into. **This is the single number that separates a
character from a capsule**: at zero, a kerb stops it.

Changing `Size`, `MaxSlopeAngle`, `AutoStepHeight` or `CollisionGroup` rebuilds
the controller. A capsule, a slope limit and a step height are things it is
constructed with, and a character that changes size mid-stride is a rare enough
event to pay for.

## Characters block, they do not push

Two characters block each other and neither pushes the other. Walking into
somebody standing still stops you and leaves them where they were, however fast
you were going and however long you keep walking.

Shoving, knockback and crowd flow are game rules, and a game writes them by
moving the other character itself.

Whether two characters collide at all is decided by `BasePart.CollisionGroup`
like any other pair.

## What is not here

No `Health`, no `Died`, no default animation set, no state machine. Those were a
game's rules living in the engine. `BasePart.Density` is inherited but does not
tune a character — the controller carries its own mass.

## Where to look next

- [Rigid bodies](manual:physics/bodies) — everything `CharacterBody` inherits
- [Actions, bindings and contexts](manual:input/actions) — where
  `moveDirection` comes from
- [`CharacterBody`](api:CharacterBody)
