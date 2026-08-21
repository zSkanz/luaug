--!strict

-- examples/02-meshes — the M4 deliverable.
--
-- A glTF scene, lit by a real forward PBR pass, with a shadow-casting sun whose
-- angle is a property a script can drive. Everything M1, M2 and M3 could show
-- was a wire box; this is the first example whose subject is what the engine
-- actually draws.
--
-- What each part is here to demonstrate, rather than to look nice:
--
--   * **A `MeshPart` is a file.** One glTF per part, which is the shape
--     `api-design.md` §2.6's prefab example already assumes. The file's own
--     materials arrive with it -- four of them, sweeping metalness and
--     roughness, because a scene where everything is the same dielectric proves
--     the shader ran and not that it is right.
--   * **`Camera` is an ordinary instance.** Nothing in the engine owns it: this
--     script creates it, assigns `Workspace.CurrentCamera`, and moves it from
--     `Heartbeat` like any other object. There is no CameraType and no
--     controller to fight.
--   * **The sun is a pure function of `ClockTime`.** Dragging the day/night
--     slider in the F3 overlay writes that property and nothing else, and the
--     lighting follows -- so a replay at the same tick lights identically
--     (R10). The orbit below drives it from the SimClock for the same reason.
--   * **Point lights are children of parts.** A light with no `BasePart`
--     ancestor lights nothing, which is the attach model `api-design.md` §2.2
--     specifies and is what makes "move the lamp" mean "move the part".
--   * **`Transparency` fades.** The pane is its own `MeshPart`, because
--     `Transparency` is a per-instance property: a fifth node inside
--     `scene.gltf` would be a fifth section of one part and would have nothing
--     to set it on. It fades on the same clock the sun moves on, so one value
--     drives everything in the scene that changes.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- The scene file. One `MeshPart`, because one file is one mesh.
local SceneContent: Content = "asset://models/scene.gltf"

--- The pane, in its own file for the reason in the header: `Transparency` is a
--- property of an instance, so demonstrating it needs an instance of its own.
local PaneContent: Content = "asset://models/pane.gltf"

--- Seconds of real time for one full day. Short enough that the shadow moves
--- while you watch and long enough that it does not strobe.
local DayLengthSeconds = 90

--- Metres. Wide enough to hold the whole slab in frame at the camera's pitch.
local OrbitRadius = 11
local OrbitHeight = 5.5
--- Seconds per camera revolution. Deliberately not a divisor of the day length,
--- so the camera and the sun do not fall into a repeating pair.
local OrbitPeriodSeconds = 47

-- Anchored, from M5 on, and this line is the whole difference between a scene
-- and a pile: an unanchored part is a rigid body, and a rigid body under
-- gravity falls. Everything in this example is scenery a camera orbits, so
-- everything in it says so.
local scene = Instance.new("MeshPart")
scene.Name = "Scene"
scene.MeshContent = SceneContent
scene.Anchored = true
scene.Parent = workspace

-- The pane, upright behind the cubes, so what shows through it is geometry
-- rather than sky. A fade judged against a flat background is not judged.
local pane = Instance.new("MeshPart")
pane.Name = "Pane"
pane.MeshContent = PaneContent
pane.CFrame = CFrame.new(-1.6, 1.4, 2.9)
pane.Anchored = true
pane.Parent = workspace

local camera = Instance.new("Camera")
camera.Name = "OrbitCamera"
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.Parent = workspace
workspace.CurrentCamera = camera

-- A lamp: an anchor part carrying a PointLight, so the light moves when the
-- part does. The part itself is a `Part` rather than a `MeshPart` because it is
-- a position, not geometry -- the debug path draws it as a wire box, which is
-- exactly what you want to see when you are asking where the light is.
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

Lighting.ClockTime = 8
Lighting.GeographicLatitude = 35
Lighting.Ambient = Color3.fromRGB(10, 11, 15)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

local elapsed = 0

RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt

    -- Driven from the SimClock rather than from a wall clock, so a replay of
    -- this example lights and frames identically (R10).
    Lighting.ClockTime = (Lighting.ClockTime + (24 / DayLengthSeconds) * dt) % 24

    -- The pane fades on the same clock as the sun: nearly clear at midnight,
    -- nearly solid at noon. Every value in between is rendered, which is the
    -- whole difference from an alpha cutout -- at a 0.5 cutoff 0.4 is fully
    -- opaque and 0.6 is gone, and nothing fades.
    pane.Transparency = 0.15 + 0.35 * (1 + math.cos((Lighting.ClockTime / 24) * math.pi * 2))

    local angle = (elapsed / OrbitPeriodSeconds) * math.pi * 2
    local eye = Vector3.new(math.cos(angle) * OrbitRadius, OrbitHeight, math.sin(angle) * OrbitRadius)
    camera.CFrame = CFrame.lookAt(eye, Vector3.new(0, 1, 0))
end)
