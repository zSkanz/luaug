--!strict

-- A small valley where the day runs on `Lighting.ClockTime`, under a `Sky` of
-- six pictures with clouds drifting across it, in an `Atmosphere`, with every
-- post effect ADR 0096 added -- each one switched on and off by a key, and a
-- list on the screen of what is on.
--
--   1  Atmosphere        5  Sun rays
--   2  Sky pictures      6  Depth of field
--   3  Bloom             7  Blur (the camera's own, as a pause menu would)
--   4  Colour grade      T  the day at ten times the speed, or back
--
-- **Everything here is an instance under `Lighting` or the camera**, which is
-- the whole idea: a script turns the look of a world on and off by setting
-- `Enabled`, or by giving an instance a parent and taking it away.

local InputService = game:GetService("InputService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local UIService = game:GetService("UIService")

-- --- The valley ---------------------------------------------------------------

local function block(name: string, shape: EnumPartShape, size: vector, at: CFrame, color: Color3): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Shape = shape
    part.Size = size
    part.CFrame = at
    part:SetMaterialParameter("Color", color)
    part.Anchored = true
    part.Parent = workspace
    return part
end

block("Floor", Enum.PartShape.Block, vector.create(700, 2, 700), CFrame.new(0, -1, 0), Color3.fromRGB(96, 122, 70))

-- Hills on both sides, falling away into the distance for the air to fill.
local random = Random.new(96)
for index = 0, 13 do
    local side = if index % 2 == 0 then -1 else 1
    local along = -40 - math.floor(index / 2) * 55
    local size = 50 + random:NextNumber() * 40
    block(
        `Hill{index}`,
        Enum.PartShape.Ball,
        vector.create(size, size, size),
        CFrame.new(side * (60 + random:NextNumber() * 30), -size * 0.25, along),
        Color3.fromRGB(88, 110, 64)
    )
end

-- A stand of trees, a pond and a cabin with a lamp for the night.
for index = 0, 17 do
    local x = -25 + random:NextNumber() * 50
    local z = -20 - random:NextNumber() * 90
    block(
        `Trunk{index}`,
        Enum.PartShape.Block,
        vector.create(0.7, 7, 0.7),
        CFrame.new(x, 3.5, z),
        Color3.fromRGB(78, 58, 40)
    )
    block(`Crown{index}`, Enum.PartShape.Ball, vector.create(5, 5, 5), CFrame.new(x, 8, z), Color3.fromRGB(54, 96, 50))
end
local pond = block(
    "Pond",
    Enum.PartShape.Block,
    vector.create(26, 0.2, 18),
    CFrame.new(18, 0.05, -10),
    Color3.fromRGB(60, 110, 150)
)
pond:SetMaterialParameter("Transparency", 0.2)
local cabin =
    block("Cabin", Enum.PartShape.Block, vector.create(8, 5, 6), CFrame.new(-6, 2.5, -8), Color3.fromRGB(150, 110, 80))
block("Roof", Enum.PartShape.Block, vector.create(9, 1, 7), CFrame.new(-6, 5.5, -8), Color3.fromRGB(90, 60, 50))
local lamp = Instance.new("PointLight")
lamp.Color = Color3.fromRGB(255, 200, 140)
lamp.Range = 22
lamp.Parent = cabin

-- --- The camera, turning slowly round the cabin -------------------------------

local camera = Instance.new("Camera")
camera.FieldOfView = 60
camera.FarPlane = 2000
camera.Parent = workspace
workspace.CurrentCamera = camera

-- --- The look -----------------------------------------------------------------

local sky = Instance.new("Sky")
sky.SkyboxBack = "asset://sky/back.png" :: Content
sky.SkyboxDown = "asset://sky/down.png" :: Content
sky.SkyboxFront = "asset://sky/front.png" :: Content
sky.SkyboxLeft = "asset://sky/left.png" :: Content
sky.SkyboxRight = "asset://sky/right.png" :: Content
sky.SkyboxUp = "asset://sky/up.png" :: Content
sky.CloudCover = 0.45
sky.Parent = Lighting

local air = Instance.new("Atmosphere")
air.Density = 0.18
air.Haze = 0.5
air.Glare = 1
air.Parent = Lighting

local bloom = Instance.new("BloomEffect")
bloom.Intensity = 1.5
bloom.Parent = Lighting

local grade = Instance.new("ColorCorrectionEffect")
grade.TintColor = Color3.fromRGB(255, 244, 232)
grade.Contrast = 0.1
grade.Saturation = 0.1
grade.Parent = Lighting

local rays = Instance.new("SunRaysEffect")
rays.Intensity = 0.3
rays.Parent = Lighting

local focus = Instance.new("DepthOfFieldEffect")
focus.FocusDistance = 30
focus.InFocusRadius = 15
focus.FarIntensity = 0.4
focus.NearIntensity = 0.6
focus.Enabled = false
focus.Parent = Lighting

-- The viewer's own blur, on the camera: a pause menu's, which nobody else in a
-- networked game would see.
local blur = Instance.new("BlurEffect")
blur.Size = 16
blur.Enabled = false
blur.Parent = camera

-- --- The list on the screen ---------------------------------------------------

local screen = Instance.new("ScreenGui")
screen.Parent = UIService
local list = Instance.new("TextLabel")
list.Size = UDim2.fromOffset(300, 196)
list.Position = UDim2.fromOffset(16, 16)
list.TextSize = 16
list.TextXAlignment = Enum.HorizontalAlignment.Left
list.TextColor = Color3.fromRGB(255, 255, 255)
list.BackgroundColor = Color3.fromRGB(20, 24, 36)
list.BackgroundTransparency = 0.35
list.Parent = screen

local fast = false

local function mark(on: boolean): string
    return if on then "on " else "off"
end

local function show()
    list.Text = table.concat({
        `1  Atmosphere      {mark(air.Parent == Lighting)}`,
        `2  Sky pictures    {mark(sky.SkyboxUp ~= "")}`,
        `3  Bloom           {mark(bloom.Enabled)}`,
        `4  Colour grade    {mark(grade.Enabled)}`,
        `5  Sun rays        {mark(rays.Enabled)}`,
        `6  Depth of field  {mark(focus.Enabled)}`,
        `7  Blur            {mark(blur.Enabled)}`,
        `T  Day x10         {mark(fast)}`,
        string.format("   %05.2f h", Lighting.ClockTime),
    }, "\n")
end

local pictures = {
    Back = sky.SkyboxBack,
    Down = sky.SkyboxDown,
    Front = sky.SkyboxFront,
    Left = sky.SkyboxLeft,
    Right = sky.SkyboxRight,
    Up = sky.SkyboxUp,
}

InputService.InputBegan:Connect(function(input: InputObject, consumed: boolean)
    if consumed then
        return
    end
    local key = input.KeyCode
    if key == Enum.KeyCode.Digit1 then
        -- The air is on while it is under `Lighting`; anywhere else it does
        -- nothing, and the linear fog comes back.
        air.Parent = if air.Parent == Lighting then nil else Lighting
    elseif key == Enum.KeyCode.Digit2 then
        -- Without pictures a `Sky` keeps its clouds, sun and stars over the
        -- engine's own gradient.
        local on = sky.SkyboxUp ~= ""
        sky.SkyboxBack = if on then "" :: Content else pictures.Back
        sky.SkyboxDown = if on then "" :: Content else pictures.Down
        sky.SkyboxFront = if on then "" :: Content else pictures.Front
        sky.SkyboxLeft = if on then "" :: Content else pictures.Left
        sky.SkyboxRight = if on then "" :: Content else pictures.Right
        sky.SkyboxUp = if on then "" :: Content else pictures.Up
    elseif key == Enum.KeyCode.Digit3 then
        bloom.Enabled = not bloom.Enabled
    elseif key == Enum.KeyCode.Digit4 then
        grade.Enabled = not grade.Enabled
    elseif key == Enum.KeyCode.Digit5 then
        rays.Enabled = not rays.Enabled
    elseif key == Enum.KeyCode.Digit6 then
        focus.Enabled = not focus.Enabled
    elseif key == Enum.KeyCode.Digit7 then
        blur.Enabled = not blur.Enabled
    elseif key == Enum.KeyCode.T then
        fast = not fast
    else
        return
    end
    show()
end)

-- --- The day ------------------------------------------------------------------
--
-- Six minutes from dawn to dawn, starting at seven in the morning -- or ten
-- times that. Accumulated from the tick's own step, so a paused game's sun
-- stands still with its clouds.

local clock = 7

RunService.Heartbeat:Connect(function(dt: number)
    clock = (clock + dt * (24 / 360) * (if fast then 10 else 1)) % 24
    Lighting.ClockTime = clock

    -- The lamp comes up as the day goes down, through dusk rather than at a
    -- step.
    local daylight = math.clamp(math.sin((clock - 6) / 24 * math.pi * 2) * 1.4 + 0.35, 0, 1)
    lamp.Brightness = 4 * (1 - daylight)

    local turn = RunService.SimTime * 0.05
    camera.CFrame = CFrame.lookAt(
        vector.create(math.cos(turn) * 34 - 6, 7, math.sin(turn) * 34 - 8),
        cabin.Position + vector.create(0, 2, 0)
    )
    show()
end)

show()
