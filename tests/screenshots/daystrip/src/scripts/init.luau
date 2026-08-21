--!strict

-- **Anchored, and that line was missing until M7.5.** An unanchored part has
-- been a rigid body since M5, and a rigid body under gravity falls -- which this
-- scene never showed because it only ever ran for eight frames. Holding each
-- hour long enough for the automatic exposure to settle made it run for three
-- hundred, and the scene dropped out of frame.
--
-- The M4.5 deliverable, rendered as evidence: `examples/02-meshes`'s scene from
-- a **fixed** camera, one frame per hour of the day.
--
-- The example itself orbits, which is right for something a person watches and
-- wrong for something a person is asked to judge: two variables in one strip
-- and nobody can say which one moved the shadow. Here the camera stands still,
-- so the sun and the pane's `Transparency` are the only things that change --
-- and both are functions of one number.
--
-- Copied rather than shared with the example, for the reason the capture gate
-- states about its own copy: evidence that reaches into an example breaks when
-- someone edits the example, and the example is meant to be edited.
--
-- **One frame per run.** `ClockTime` is a function of the tick count, so
-- `--frames=N` selects the hour and the screenshot the host writes at exit is
-- that hour's frame. `scripts/daystrip.ps1` runs it once per hour and lays the
-- results side by side with `imgstrip`.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- Hours per rendered frame. Eight frames covers a day at three-hour steps,
--- which is enough for the shadow to sweep and short enough to look at.
local HoursPerFrame = 3

--- How many frames each hour is HELD for before the strip's frame is taken.
---
--- One frame per hour was enough until M7.5, and then it was not: automatic
--- exposure adapts over frames, so a clock that jumped three hours every frame
--- produced a strip whose night frames were still exposed for the afternoon.
--- The strip was showing the harness rather than the renderer. Forty frames is
--- about seven eighths of the way to converged at the shipped rate.
local FramesPerHour = 40
--- The first frame's hour. Not midnight: a strip that opens with two black
--- frames spends a quarter of itself saying nothing.
local StartHour = 6

local scene = Instance.new("MeshPart")
scene.Name = "Scene"
scene.MeshContent = "asset://models/scene.gltf" :: Content
scene.Anchored = true
scene.Parent = workspace

local pane = Instance.new("MeshPart")
pane.Name = "Pane"
pane.MeshContent = "asset://models/pane.gltf" :: Content
pane.CFrame = CFrame.new(-1.6, 1.4, 2.9)
pane.Anchored = true
pane.Parent = workspace

local lamp = Instance.new("Part")
lamp.Name = "Lamp"
lamp.Size = Vector3.new(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
lamp.Anchored = true
lamp.Parent = workspace

local lampLight = Instance.new("PointLight")
lampLight.Color = Color3.fromRGB(255, 176, 92)
lampLight.Brightness = 14
lampLight.Range = 9
lampLight.Parent = lamp

local camera = Instance.new("Camera")
camera.FieldOfView = 38
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.CFrame = CFrame.lookAt(Vector3.new(7.2, 4.2, 8.2), Vector3.new(-0.4, 1.3, 0.0))
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.GeographicLatitude = 35
Lighting.Ambient = Color3.fromRGB(10, 11, 15)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

local tick = 0

local function apply()
    Lighting.ClockTime = (StartHour + math.floor(tick / FramesPerHour) * HoursPerFrame) % 24
    pane.Transparency = 0.15 + 0.35 * (1 + math.cos((Lighting.ClockTime / 24) * math.pi * 2))
end

-- On the boot drain, so a one-frame run renders hour six rather than the
-- defaults.
apply()

RunService.Heartbeat:Connect(function()
    tick += 1
    apply()
end)
