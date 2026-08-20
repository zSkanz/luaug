--!strict

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
--- The first frame's hour. Not midnight: a strip that opens with two black
--- frames spends a quarter of itself saying nothing.
local StartHour = 6

local scene = Instance.new("MeshPart")
scene.Name = "Scene"
scene.MeshContent = "asset://models/scene.gltf" :: Content
scene.Parent = workspace

local pane = Instance.new("MeshPart")
pane.Name = "Pane"
pane.MeshContent = "asset://models/pane.gltf" :: Content
pane.CFrame = CFrame.new(-1.6, 1.4, 2.9)
pane.Parent = workspace

local lamp = Instance.new("Part")
lamp.Name = "Lamp"
lamp.Size = Vector3.new(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
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
Lighting.Ambient = Color3.fromRGB(38, 42, 56)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

local tick = 0

local function apply()
    Lighting.ClockTime = (StartHour + tick * HoursPerFrame) % 24
    pane.Transparency = 0.15 + 0.35 * (1 + math.cos((Lighting.ClockTime / 24) * math.pi * 2))
end

-- On the boot drain, so a one-frame run renders hour six rather than the
-- defaults.
apply()

RunService.Heartbeat:Connect(function()
    tick += 1
    apply()
end)
