--!strict
-- ADR 0091's end-to-end proof: parts wearing a material that names a surface
-- shader the USER wrote (a copy of the built-in surface, in this project's
-- content), compiled by the editor's compiler at run time. `../builtin` is the
-- same scene with the same material naming no shader; the two must draw
-- identically.
local look = Material.load("asset://materials/look.material.json")

local floor = Instance.new("Part")
floor.Anchored = true
floor.Size = vector.create(24, 1, 24)
floor.Position = vector.create(0, -0.5, 0)
floor.Parent = workspace

for index = 0, 4 do
    local block = Instance.new("Part")
    block.Anchored = true
    block.Size = vector.create(1.5, 1 + index * 0.5, 1.5)
    block.Position = vector.create(index * 2.5 - 5, (1 + index * 0.5) / 2, 0)
    block.Material = look
    if index % 2 == 0 then
        block:SetMaterialParameter("Color", Color3.fromRGB(60, 120, 200))
    end
    block.Parent = workspace
end

local camera = Instance.new("Camera")
camera.FieldOfView = 50
camera.CFrame = CFrame.lookAt(vector.create(0, 6, 12), vector.create(0, 1, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera
