# Your first world

```bash
luaug new hello
cd hello
luaug dev
```

A window opens. Leave `luaug dev` running: every save from here rebuilds the
world in well under a second.

## Something to look at

Open `src/scripts/main.luau` and replace it:

```luau
--!strict
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

Lighting.ClockTime = 8

local camera = Instance.new("Camera")
camera.CFrame = CFrame.lookAt(vector.create(0, 6, 14), vector.create(0, 1, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

local ground = Instance.new("Part")
ground.Size = vector.create(40, 1, 40)
ground.Position = vector.create(0, -0.5, 0)
ground.Color = Color3.fromRGB(90, 108, 82)
ground.Anchored = true
ground.Parent = workspace

local crate = Instance.new("Part")
crate.Size = vector.create(2, 2, 2)
crate.Position = vector.create(0, 8, 0)
crate.Color = Color3.fromRGB(200, 140, 60)
crate.Parent = workspace

RunService.Heartbeat:Connect(function(dt: number)
    Lighting.ClockTime += dt * 0.4
end)
```

Save. A crate falls onto a green floor while the sun moves.

Six things happened there, and each is a page of its own later:

- **`game:GetService`** reached two singletons.
- **`Instance.new`** made parts, and **`Parent`** put them in the world.
- **`Anchored`** made one of them scenery and left the other to gravity.
- **`CFrame.lookAt`** aimed a camera, and **`Workspace.CurrentCamera`** made it
  the view.
- **`RunService.Heartbeat`** ran code once per simulation tick with a fixed
  `dt`.
- **`Lighting.ClockTime`** moved the sun, and everything else followed.

## Make it react

Add this to the end:

```luau
crate.Touched:Connect(function(other: BasePart)
    if other == ground then
        crate.Color = Color3.fromRGB(80, 180, 120)
    end
end)
```

Save. The crate lands and turns green.

`BasePart.Touched` is **deferred**, like every signal here: it is enqueued when
the contact happens and your handler runs at the next resumption point, not
inside the solver. That is one rule rather than two modes, and
[Signals and connections](manual:concepts/signals) is where it is spelled out.

## Make it yours

```luau
crate:SetAttribute("Bouncy", true)
crate:AddTag("Pickup")
```

An **attribute** is your own typed value on an instance. A **tag** is a name you
can find a whole set by:

```luau
--!strict
local TagService = game:GetService("TagService")

TagService:GetInstanceAddedSignal("Pickup"):Connect(function(instance: Instance)
    print("a pickup appeared:", instance.Name)
end)
```

That handler runs for every instance that ever carries the tag — one you spawn,
one a scene declared, one streaming materialised — without knowing where it came
from.

## Add a character

```luau
--!strict
local character = Instance.new("CharacterBody")
character.Size = vector.create(2, 5, 2)
character.Position = vector.create(0, 6, 8)
character.Parent = workspace

RunService.Heartbeat:Connect(function(dt: number)
    character:Move(vector.create(0, 0, -1))
end)
```

A capsule walks forward, climbs the ramp it meets and steps over a kerb.
`CharacterBody.Move` is intent for **one tick** — a character told nothing
stops, which is why it is called every tick.

## Every file is strict

`--!strict` at the top of every file is the convention, and the analyzer is
configured for it. Run:

```bash
luaug check
```

`crate.Anchorred = true` is an error there rather than a surprise later. That is
the whole return on a fully typed API surface.

## What to read next

Three different next steps, depending on what you are:

- **Coming from Roblox?** [The migration guide](manual:roblox/migration) is the
  short version of everything that is spelled differently.
- **Want the model?** [The Instance tree](manual:concepts/instance-tree), then
  [Signals and connections](manual:concepts/signals), then
  [The frame, phase by phase](manual:concepts/frame).
- **Want to build something?** [Parts and solids](manual:world/parts) and
  [Rigid bodies](manual:physics/bodies).
