--!strict

-- The scene ADR 0096's captures and measurements are made in.
--
-- A small valley at late afternoon, seen looking west into the sun: a ground,
-- two ridges that fall away into the distance, a row of pillars receding
-- towards the horizon, a glowing lamp, four coloured blocks and a dark frame
-- standing between the camera and the sun. Everything one of the effects
-- changes is in one picture -- a highlight to bloom, colours to grade, depth to
-- focus by, a sun to stream past something, distance for the air to fill -- so
-- every before and after is taken from the same place at the same hour.
--
-- **Which effects are in the world is `Variant`**, and nothing else changes.
-- `tools/repo/look_captures.py` copies this project and rewrites that one line
-- per capture; run as it is, the scene is the "before" of every pair.

local Lighting = game:GetService("Lighting")

local Variant = "none"

local function block(name: string, size: vector, at: CFrame, color: Color3): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = size
    part.CFrame = at
    part:SetMaterialParameter("Color", color)
    part.Anchored = true
    part.Parent = workspace
    return part
end

-- The valley floor and its two ridges, long enough to reach the horizon.
block("Ground", vector.create(900, 1, 240), CFrame.new(-400, -0.5, 0), Color3.fromRGB(88, 104, 72))
for index = 0, 7 do
    local x = -30 - index * 80
    local height = 10 + index * 6
    block(
        `RidgeNorth{index}`,
        vector.create(70, height, 40),
        CFrame.new(x, height / 2, -70),
        Color3.fromRGB(96, 92, 84)
    )
    block(
        `RidgeSouth{index}`,
        vector.create(70, height * 0.8, 40),
        CFrame.new(x - 30, height * 0.4, 75),
        Color3.fromRGB(90, 88, 80)
    )
end

-- Pillars receding along the valley: near ones for depth of field to soften,
-- far ones for the air to swallow.
for index = 0, 11 do
    local x = -12 - index * 22
    block(`Pillar{index}`, vector.create(1.5, 9, 1.5), CFrame.new(x, 4.5, -9), Color3.fromRGB(210, 206, 196))
end

-- Four colours, for a grade to move.
local colors = {
    Color3.fromRGB(200, 40, 36),
    Color3.fromRGB(46, 170, 60),
    Color3.fromRGB(40, 80, 210),
    Color3.fromRGB(230, 190, 40),
}
for index, color in colors do
    block(`Swatch{index}`, vector.create(2, 2, 2), CFrame.new(-14, 1, 2 - index * 3), color)
end

-- The lamp: bright enough to bloom at this hour.
local lamp = block("Lamp", vector.create(1.2, 1.2, 1.2), CFrame.new(-9, 3.5, 4), Color3.fromRGB(255, 230, 190))
-- Its material glows above one on purpose: a lamp is brighter than white
-- paper in the same light.
lamp.Material = Material.load("asset://materials/lamp.material.json" :: Content)

-- A dark frame across the sun, for the rays to stream past.
block("FramePostA", vector.create(0.6, 16, 0.6), CFrame.new(-40, 8, -3), Color3.fromRGB(40, 36, 32))
block("FramePostB", vector.create(0.6, 16, 0.6), CFrame.new(-40, 8, 5), Color3.fromRGB(40, 36, 32))
block("FrameBeam", vector.create(0.6, 0.6, 9), CFrame.new(-40, 12, 1), Color3.fromRGB(40, 36, 32))
block(
    "FrameBrace",
    vector.create(0.6, 0.6, 12),
    CFrame.new(-40, 7, 1) * CFrame.fromEuler(0.6, 0, 0),
    Color3.fromRGB(40, 36, 32)
)

local camera = Instance.new("Camera")
camera.FieldOfView = 60
camera.NearPlane = 0.1
camera.FarPlane = 1500
camera.CFrame = CFrame.lookAt(vector.create(6, 3, 1), vector.create(-60, 9, 1))
camera.Parent = workspace
workspace.CurrentCamera = camera

-- Late afternoon on the equator: the sun is fifteen degrees above the western
-- horizon, which is where the camera looks.
Lighting.ClockTime = 17
Lighting.GeographicLatitude = 0
Lighting.Brightness = 2.6

-- Every effect a capture asks for, by name. The world's effects go under
-- `Lighting`; the ones a single viewer would own go on the camera.
local Variants: { [string]: () -> () } = {
    none = function() end,
    ["bloom-off"] = function()
        local bloom = Instance.new("BloomEffect")
        bloom.Enabled = false
        bloom.Parent = Lighting
    end,
    ["bloom-strong"] = function()
        local bloom = Instance.new("BloomEffect")
        bloom.Intensity = 4
        bloom.Size = 48
        bloom.Threshold = 0.6
        bloom.Parent = Lighting
    end,
    ["grade-warm"] = function()
        local grade = Instance.new("ColorCorrectionEffect")
        grade.TintColor = Color3.fromRGB(255, 226, 196)
        grade.Contrast = 0.2
        grade.Saturation = 0.25
        grade.Parent = Lighting
    end,
    ["grade-mono"] = function()
        local grade = Instance.new("ColorCorrectionEffect")
        grade.Saturation = -1
        grade.Contrast = 0.3
        grade.Brightness = -0.05
        grade.Parent = Lighting
    end,
}

local apply = Variants[Variant]
assert(apply ~= nil, `no variant called {Variant}`)
apply()
