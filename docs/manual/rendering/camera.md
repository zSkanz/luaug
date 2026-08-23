# The camera

A `Camera` is an ordinary instance a script owns and moves. The engine never
takes it over, which is why there is no camera-type state machine and no hidden
controller to fight with.

## Making one and using it

```luau
--!strict
local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.Parent = workspace

workspace.CurrentCamera = camera
```

`Workspace.CurrentCamera` is the whole of "make this the view". It is nullable,
and **`nil` renders nothing** rather than falling back to a camera the engine
invented — a view nobody asked for is harder to debug than a black frame that
says why.

## Moving it

Write `Camera.CFrame`. That is the only mechanism, and `CFrame.lookAt` is
usually the shortest way to say what you mean:

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.PreRender:Connect(function(dt: number)
    local eye = subject.Position + vector.create(0, 6, 11)
    camera.CFrame = CFrame.lookAt(eye, subject.Position + vector.create(0, 2, 0))
end)
```

The camera looks along its `CFrame.LookVector`, which is **−Z**.

`Camera` is a `PVInstance`, so `PVInstance.PivotTo` works on it too.

## Which phase to move it in

It depends on what the camera follows, and getting it wrong produces a symptom
that looks like a rendering bug:

- **A camera that follows a simulated thing belongs on
  `RunService.Heartbeat`.** A camera advanced on the render clock moves every
  frame while the world it looks at moves once a tick, and the difference reads
  as the whole world vibrating.
- **A camera driven only by pointer input can go on `RunService.PreRender`**,
  which is also where a purely cosmetic camera shake belongs.

`PreRender` never fires in a headless run, which is a second reason a camera
that matters to gameplay should not live there.

## The three numbers

| Property | Unit | Default |
|---|---|---|
| `Camera.FieldOfView` | Degrees, **vertical** | 70 |
| `Camera.NearPlane` | Metres | 0.1 |
| `Camera.FarPlane` | Metres | 5000 |

`FieldOfView` accepts more than zero and less than 180, open at both ends.

**Raise the near plane rather than lowering it.** Small values buy very little
and cost depth precision across the whole scene — a near plane of 0.01 makes
distant geometry fight with itself.

## Camera rigs

`@luaug/camera` ships two, so that a third-person game does not start by writing
spherical coordinates:

```luau
--!strict
local RunService = game:GetService("RunService")
local camera = require("@luaug/camera")

local rig = camera.thirdPerson({
    Subject = character,
    Distance = 11,
    Height = 6,
    Focus = 2,
})

RunService.Heartbeat:Connect(function(dt: number)
    rig:Turn(stickDelta, dt)   -- a RATE: takes dt
    rig:Update(dt)
end)
```

`camera.thirdPerson` creates the `Camera`, parents it to `workspace`, assigns
`Workspace.CurrentCamera`, and places it correctly before the first frame rather
than easing in from the origin. `camera.orbit` is the same rig with a constant
yaw rate, which is what a menu backdrop or a turntable wants.

Two rules the module exists to enforce, and both are worth knowing even if you
write your own rig:

- **Advance a rig on `Heartbeat`, not `PreRender`** — for the reason above.
- **Never scale a pointer delta by `dt`.** `Turn` takes a stick deflection and a
  `dt` because a stick is a rate. `Look` takes a mouse delta and no `dt` because
  a mouse delta is already a displacement, and multiplying it by frame time
  makes the sensitivity depend on the frame rate.

`rig:Basis()` returns the rig's forward and right vectors, flattened, which is
what turns a two-axis input into a world-space movement direction.

## What is not here

There is no `ViewportSize`, no `WorldToViewportPoint` and no
`ViewportPointToRay`. A script cannot currently project a world point to a
screen coordinate; `UIService.SafeAreaInsets` and the UI tree are what
screen-space work is built on.

## Where to look next

- [Lighting and the sky](manual:rendering/lighting)
- [The frame, phase by phase](manual:concepts/frame) — why the phase matters
- [`Camera`](api:Camera)
