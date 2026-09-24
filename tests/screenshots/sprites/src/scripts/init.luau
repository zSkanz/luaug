--!strict

-- The evidence scene for the 2D layer's sprite pass (phase 3).
--
-- One orthographic camera over a painted tilemap and one of everything a
-- `Part2D` can be drawn as: a box, a circle and a capsule cut out of their
-- rectangles, a turned box, a picture through the Nearest filter, and a
-- half-transparent strip whose `ZIndex` puts it over the turned box it
-- crosses. The frame is exact to the metre: ten metres tall across 720
-- pixels, the ground's top at y = 0 on row 576, so a camera or a projection
-- that drifted shows as everything moving rather than as a colour.
--
-- Anchored throughout, so the physics does not move anything between runs.

local camera = Instance.new("Camera")
camera.Parent = workspace
workspace.CurrentCamera = camera
camera.Projection = Enum.CameraProjection.Orthographic
camera.OrthographicSize = 5
camera.CFrame = CFrame.new(2, 3, 30)

-- Tile 1 is grass over dirt and tile 2 is dirt, 16 pixels each.
local ground = Instance.new("Tilemap2D")
ground.Tileset = "asset://sprites/tiles.png" :: Content
ground:FillRect(-12, -2, 16, -2, 2)
ground:FillRect(-12, -1, 16, -1, 1)
ground:FillRect(4, 0, 7, 0, 1)
ground.Parent = workspace

local function part(position: Vector2, size: Vector2, color: Color3): Part2D
    local created = Instance.new("Part2D")
    created.Position = position
    created.Size = size
    created.Color = color
    created.Anchored = true
    created.Parent = workspace
    return created
end

part(Vector2.new(-4, 0.5), Vector2.new(1, 1), Color3.fromRGB(220, 60, 60))

local ball = part(Vector2.new(-2, 1), Vector2.new(2, 2), Color3.fromRGB(60, 110, 230))
ball.Shape = Enum.Shape2D.Circle

local pill = part(Vector2.new(0.5, 1), Vector2.new(1, 2), Color3.fromRGB(240, 200, 60))
pill.Shape = Enum.Shape2D.Capsule

local tilted = part(Vector2.new(2.5, 2), Vector2.new(1.5, 1), Color3.fromRGB(160, 80, 200))
tilted.Rotation = 30
tilted.ZIndex = 2

local hero = part(Vector2.new(5.5, 1.5), Vector2.new(1, 1), Color3.fromRGB(255, 255, 255))
hero.Image = "asset://sprites/face.png" :: Content
hero.Filter = Enum.TextureFilter.Nearest

local strip = part(Vector2.new(2, 1.5), Vector2.new(3, 0.5), Color3.fromRGB(255, 255, 255))
strip.Transparency = 0.5
strip.ZIndex = 3
