--!strict

-- D182's regression scene: enough draws that each bind every sampler the
-- forward pass declares to run a D3D12 GPU descriptor heap past its end.
--
-- Transparent parts because the blended pass never instances -- its order is its
-- correctness -- so every one of them is a draw of its own however the opaque
-- pass batches, and this scene keeps reaching the heap's end after D184 made
-- differently coloured opaque parts one call. The forward pass binds thirteen
-- samplers a draw into a heap of 2048, which does not divide: a draw starting
-- a few slots short of the end is written past it.
--
-- **The count changes every frame**, so the heaps are replaced mid-frame at a
-- different draw each time -- the path the SDL patch changed, which now has to
-- rebind every table it replaced under.
--
-- **What this cannot promise is to fail without the patch.** The debug layer
-- reports an overflowing write only when it lands outside EVERY descriptor
-- heap, and where it lands is the driver's address layout: unpatched, this
-- scene ran clean in ten runs of up to 1500 frames while the game that found
-- D182 died in one of one (D182's row says so). It is the scene that walks the
-- replacement path, run under the layer, on every gate.

local RunService = game:GetService("RunService")

local Rows = 30
local Columns = 40

local parts: { BasePart } = {}
for row = 1, Rows do
    for column = 1, Columns do
        local part = Instance.new("Part")
        part.Anchored = true
        part.Size = Vector3.new(1, 1, 1)
        part.Position = Vector3.new((column - Columns / 2) * 1.2, (row - Rows / 2) * 1.2, -45)
        part:SetMaterialParameter("Color", Color3.new(column / Columns, row / Rows, 0.5))
        part:SetMaterialParameter("Transparency", 0.5)
        part.Parent = workspace
        table.insert(parts, part)
    end
end

-- A script run on its own has no scene and so no camera; without one nothing
-- is drawn and the test would pass by drawing nothing.
local camera = workspace.CurrentCamera
if camera == nil then
    local made = Instance.new("Camera")
    made.Parent = workspace
    workspace.CurrentCamera = made
    camera = made
end
assert(camera, "the scene needs a camera to draw anything")
camera.CFrame = CFrame.lookAt(Vector3.new(0, 0, 0), Vector3.new(0, 0, -45))

-- Each tick shows a different number of them: the rest move behind the camera.
local ticks = 0
RunService.Heartbeat:Connect(function()
    ticks += 1
    local shown = 200 + (ticks * 53) % (#parts - 200)
    for index, part in parts do
        local row = (index - 1) // Columns
        local column = (index - 1) % Columns
        local z = if index <= shown then -45 else 45
        part.Position = Vector3.new((column - Columns / 2) * 1.2, (row - Rows / 2) * 1.2, z)
    end
end)

print(`descriptor heap scene: {#parts} transparent parts`)
