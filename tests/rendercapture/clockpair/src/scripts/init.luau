--!strict

-- The M4.5 gate addition: **two frames whose only difference is `ClockTime`,
-- and they must not be identical.**
--
-- The six-frame meshes golden cannot make this claim and never could. A golden
-- says "the same as the last recording", which is exactly what it said for the
-- whole of M4 while the renderer lit every scene with `RenderEnvironment`'s
-- defaults -- a sun pinned straight up, whatever the scene asked for. Re-record
-- a golden against a defect and the golden certifies the defect.
--
-- What a differential says instead is: *change one input the output must depend
-- on, and the output must change.* Nothing about the recording history is
-- involved, so it cannot be re-recorded into agreement.
--
-- The camera is fixed and every other property is a literal, so the clock is the
-- only variable across the two frames. The driver
-- (`run_clock_differential.cmake`) additionally requires the two frames to have
-- the SAME number of commands: what must move is the values a frame carries,
-- not its shape. A frame that differed by having more draws in it would be a
-- different scene rather than a different sun.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- Morning and late afternoon: far enough apart that the sun is on opposite
--- sides of the sky, so no quantization grid can round the two together.
local Morning = 7.0
local Evening = 17.0

local scene = Instance.new("MeshPart")
scene.Name = "Scene"
scene.MeshContent = "asset://models/scene.gltf" :: Content
scene.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.CFrame = CFrame.lookAt(Vector3.new(9, 5, 9), Vector3.new(0, 1, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.GeographicLatitude = 35
Lighting.Ambient = Color3.fromRGB(38, 42, 56)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

-- Applied on the boot drain, so the first captured frame already carries the
-- morning sun rather than whatever the defaults are.
Lighting.ClockTime = Morning

local tick = 0

-- The clock moves before the THIRD frame, and the driver compares the second
-- against the third. Frame one is not comparable to anything: it is the frame
-- that creates every pipeline, shader, sampler and buffer the scene needs, so
-- it carries a hundred and ninety commands where a steady frame carries a
-- hundred and fifty-seven. Comparing against it would fail on the resource
-- creation and never reach the question being asked.
RunService.Heartbeat:Connect(function()
    tick += 1
    if tick == 2 then
        Lighting.ClockTime = Evening
    end
end)
