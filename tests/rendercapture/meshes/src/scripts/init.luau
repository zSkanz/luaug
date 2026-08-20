--!strict

-- The M4 render gate: three camera angles by two lighting states, as six
-- consecutive frames of one command stream.
--
-- **Every value here is a literal at a named tick, and that is the whole
-- design** (M4 brief, Decision 12). An orbit driven by elapsed time would make
-- the golden depend on how many ticks had passed when the capture happened, and
-- a one-tick shift is exactly the failure M1's Finding 9 records: small enough
-- for an image comparison to forgive and large enough to change every number in
-- a command stream. M3's Finding 15 is the same lesson from the other side --
-- an absolute tick is the only thing that means the same in two different runs.
--
-- Six frames in one stream rather than six separate goldens: the frames are
-- consecutive, so a diff localises the break to a frame anyway, and one file
-- cannot drift out of step with another the way six can.
--
-- The scene is a byte-identical copy of `examples/02-meshes`'s, produced by the
-- same generator. Copied rather than shared because a gate that reaches into an
-- example is a gate that fails when someone edits the example -- and the
-- example is meant to be edited.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- Each entry is one captured frame: where the camera is, what it looks at, and
--- what hour it is. Three positions around the scene, each at two hours -- low
--- morning sun and high afternoon sun -- which is what "3 camera angles x 2
--- lighting states" means in numbers rather than in prose.
local Frames = {
    { eye = Vector3.new(9, 5, 9), look = Vector3.new(0, 1, 0), clock = 7.5 },
    { eye = Vector3.new(9, 5, 9), look = Vector3.new(0, 1, 0), clock = 15.5 },
    { eye = Vector3.new(-11, 3, 2), look = Vector3.new(0, 1.5, 0), clock = 7.5 },
    { eye = Vector3.new(-11, 3, 2), look = Vector3.new(0, 1.5, 0), clock = 15.5 },
    { eye = Vector3.new(0, 12, 0.01), look = Vector3.new(0, 0, 0), clock = 7.5 },
    { eye = Vector3.new(0, 12, 0.01), look = Vector3.new(0, 0, 0), clock = 15.5 },
}

local scene = Instance.new("MeshPart")
scene.Name = "Scene"
scene.MeshContent = "asset://models/scene.gltf" :: Content
scene.Parent = workspace

-- A second copy of the scene, far enough away that it is inside the frustum
-- from one of the three angles and outside it from another.
--
-- Without it this golden could not fail when the culler broke: the near scene is
-- one MeshPart, culling is per-mesh bounds, and every angle here frames the
-- whole of it -- so the draw count was identical whether or not anything was
-- tested. That is a gate passing while doing nothing, which this repository has
-- now found eight times.
local distant = Instance.new("MeshPart")
distant.Name = "Distant"
distant.MeshContent = "asset://models/scene.gltf" :: Content
distant.CFrame = CFrame.new(0, 0, -70)
distant.Parent = workspace

-- A transparent pane, so the blended pass is in the golden at all. Without it
-- the transparent pipeline is created, never bound, and a regression in it
-- would be invisible to the one render gate that blocks a merge.
--
-- 0.45 rather than 0 or 1: a value that is neither end of the range is the only
-- one an alpha cutout could not have produced, which is what M4.5 changed.
local pane = Instance.new("MeshPart")
pane.Name = "Pane"
pane.MeshContent = "asset://models/pane.gltf" :: Content
pane.CFrame = CFrame.new(-1.6, 1.4, 2.9)
pane.Transparency = 0.45
pane.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.Parent = workspace
workspace.CurrentCamera = camera

-- A lamp, so the point-light path is in the golden too. A gate that only ever
-- exercises the sun would not notice the punctual loop breaking.
local lamp = Instance.new("Part")
lamp.Size = Vector3.new(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
lamp.Parent = workspace

local lampLight = Instance.new("PointLight")
lampLight.Color = Color3.fromRGB(255, 176, 92)
lampLight.Brightness = 14
lampLight.Range = 9
lampLight.Parent = lamp

Lighting.GeographicLatitude = 35
Lighting.Ambient = Color3.fromRGB(38, 42, 56)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

local function apply(index: number)
    local frame = Frames[index]
    if frame == nil then
        return
    end
    camera.CFrame = CFrame.lookAt(frame.eye, frame.look)
    Lighting.ClockTime = frame.clock
end

-- Applied on the boot drain so the FIRST captured frame already carries
-- configuration one. Waiting for the first Heartbeat would leave frame one
-- showing whatever the defaults are, which is a golden of nothing.
apply(1)

local tick = 1

RunService.Heartbeat:Connect(function()
    tick += 1
    apply(tick)
end)
