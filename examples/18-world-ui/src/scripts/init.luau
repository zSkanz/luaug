--!strict
--[[
	UI in the world (F3).

	**The same UI, somewhere else.** A `SurfaceGui` lays its children out on a
	face of a part and a `BillboardGui` over a point, turned to the camera --
	and everything under them is the screen's own classes: `Frame`,
	`TextLabel` with rich text, `UICorner`. Both are drawn IN the world, so the
	crate in front of the wall hides the scoreboard behind it.

	A billboard's `Size` says two things at once: its scale is metres, so the
	sign keeps its size in the world and shrinks with distance; its offset is
	pixels, so a name tag stays readable however far away its owner is.

	**And a button on the wall is a button.** Click "Next round" on the
	scoreboard: the pointer's ray finds it in the world, exactly as a screen
	button is found under the pointer, and a crate in front of it takes the
	click instead.
]]

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

Lighting.ClockTime = 16.5

local function block(name: string, at: vector, size: vector, color: Color3): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Anchored = true
    part.Size = size
    part.Position = at
    part:SetMaterialParameter("Color", color)
    part.Parent = Workspace
    return part
end

local function corner(parent: Instance, pixels: number)
    local rounded = Instance.new("UICorner")
    rounded.CornerRadius = UDim.new(0, pixels)
    rounded.Parent = parent
end

block("Ground", vector.create(0, -0.5, 0), vector.create(60, 1, 60), Color3.fromRGB(86, 96, 82))

-- **A scoreboard on a wall**: a SurfaceGui on the wall's front face, at the
-- default fifty pixels to the metre -- so the 10 m by 6 m face is a 500 by 300
-- canvas, and a 52-pixel title is about a metre tall.
local wall = block("Wall", vector.create(0, 3, -8), vector.create(10, 6, 0.5), Color3.fromRGB(52, 56, 64))
wall.CFrame = CFrame.lookAt(vector.create(0, 3, -8), vector.create(0, 3, 10))

local board = Instance.new("SurfaceGui")
board.Face = Enum.Face.Front
board.Brightness = 1.4
board.Parent = wall

local panel = Instance.new("Frame")
panel.Size = UDim2.new(1, -40, 1, -40)
panel.Position = UDim2.new(0, 20, 0, 20)
panel.BackgroundColor = Color3.fromRGB(16, 22, 34)
panel.Parent = board
corner(panel, 18)

local title = Instance.new("TextLabel")
title.Size = UDim2.new(1, 0, 0, 80)
title.BackgroundTransparency = 1
title.TextColor = Color3.fromRGB(240, 240, 240)
title.TextSize = 52
title.RichText = true
title.Text = '<b>ROUND 3</b>  <font color="#ffb347">02:17</font>'
title.Parent = panel

local scores = Instance.new("TextLabel")
scores.Size = UDim2.new(1, -60, 1, -100)
scores.Position = UDim2.new(0, 30, 0, 84)
scores.BackgroundTransparency = 1
scores.TextColor = Color3.fromRGB(210, 214, 222)
scores.TextSize = 40
scores.RichText = true
scores.HorizontalAlignment = Enum.HorizontalAlignment.Left
scores.VerticalAlignment = Enum.VerticalAlignment.Top
scores.Text = '<font color="#6ec6ff"><b>Blue</b></font>   12<br/>'
    .. '<font color="#ff6b6b"><b>Red</b></font>    9<br/>'
    .. '<i><font size="28" transparency="0.3">first to 15 wins</font></i>'
scores.Parent = panel

local round = 3
local nextRound = Instance.new("TextButton")
nextRound.Size = UDim2.new(0, 170, 0, 56)
nextRound.Position = UDim2.new(1, -190, 1, -76)
nextRound.BackgroundColor = Color3.fromRGB(255, 179, 71)
nextRound.TextColor = Color3.fromRGB(20, 20, 20)
nextRound.TextSize = 30
nextRound.Text = "Next round"
nextRound.Parent = panel
corner(nextRound, 12)
nextRound.Activated:Connect(function()
    round += 1
    title.Text = `<b>ROUND {round}</b>  <font color="#ffb347">02:17</font>`
end)

-- **Crates with name tags.** Sized in pixels, so each tag is as big on the
-- screen near or far; a health bar is two frames.
local crates: { Part } = {}
local function crate(name: string, at: vector, health: number, color: Color3)
    local box = block(name, at, vector.create(2, 2, 2), color)
    table.insert(crates, box)

    local tag = Instance.new("BillboardGui")
    tag.Size = UDim2.new(0, 180, 0, 54)
    tag.WorldOffset = vector.create(0, 2.2, 0)
    tag.Parent = box

    local label = Instance.new("TextLabel")
    label.Size = UDim2.new(1, 0, 0, 30)
    label.BackgroundColor = Color3.fromRGB(10, 12, 18)
    label.BackgroundTransparency = 0.35
    label.TextColor = Color3.fromRGB(255, 255, 255)
    label.TextSize = 20
    label.RichText = true
    label.Text = "<b>" .. name .. "</b>"
    label.Parent = tag
    corner(label, 8)

    local bar = Instance.new("Frame")
    bar.Size = UDim2.new(1, 0, 0, 12)
    bar.Position = UDim2.new(0, 0, 0, 38)
    bar.BackgroundColor = Color3.fromRGB(40, 40, 40)
    bar.Parent = tag
    corner(bar, 6)

    local fill = Instance.new("Frame")
    fill.Size = UDim2.new(health, 0, 1, 0)
    fill.BackgroundColor = Color3.fromRGB(math.floor(255 * (1 - health)), math.floor(220 * health), 70)
    fill.Parent = bar
    corner(fill, 6)
end

crate("Scout", vector.create(-4, 1, 0), 0.9, Color3.fromRGB(200, 120, 60))
crate("Tank", vector.create(3, 1, 3), 0.35, Color3.fromRGB(120, 140, 200))
crate("Far away", vector.create(12, 1, -18), 0.6, Color3.fromRGB(180, 180, 90))

-- **A sign sized in the world**: its scale is metres, so it is a 4 m by 1 m
-- board that shrinks with distance like the post it stands on.
local post = block("Post", vector.create(-10, 1.5, -2), vector.create(0.3, 3, 0.3), Color3.fromRGB(120, 90, 60))
local sign = Instance.new("BillboardGui")
sign.Size = UDim2.new(4, 0, 1, 0)
sign.WorldOffset = vector.create(0, 2.2, 0)
sign.Parent = post
local words = Instance.new("TextLabel")
words.Size = UDim2.new(1, 0, 1, 0)
words.BackgroundColor = Color3.fromRGB(250, 240, 210)
words.TextColor = Color3.fromRGB(60, 40, 20)
words.TextSize = 30
words.RichText = true
words.Text = "<b>SPAWN</b> this way"
words.Parent = sign
corner(words, 10)

local camera = Instance.new("Camera")
camera.CFrame = CFrame.lookAt(vector.create(4, 5, 14), vector.create(0, 2, -4))
camera.Parent = Workspace
Workspace.CurrentCamera = camera

local elapsed = 0
RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt
    for index, box in crates do
        box.CFrame = CFrame.new(box.Position)
            * CFrame.fromEuler(0, elapsed * (0.5 + index * 0.2), 0, Enum.RotationOrder.XYZ)
    end
end)
