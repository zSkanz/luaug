--!strict
--- A platformer (the 2D layer, phase 3): a level painted on a `Tilemap2D`, a
--- hero that runs and jumps on it, coins to collect and a flag at the end.
---
--- Everything is on the world's XY plane and seen through an orthographic
--- `Camera`. The level is the ASCII map below, so changing it is editing text --
--- or opening the project in the editor and painting with the Tiles tool.
---
--- **The feel is the point of the controller**, and it is the four things every
--- good platformer does that a naive one does not: speed that ramps rather than
--- snaps, a jump that still works a moment after running off a ledge (coyote
--- time), a jump pressed a moment before landing that is not lost (a buffer),
--- and a short hop when the button is let go early.

local InputService = game:GetService("InputService")
local Lighting = game:GetService("Lighting")
local PhysicsService = game:GetService("PhysicsService")
local RunService = game:GetService("RunService")
local UIService = game:GetService("UIService")

Lighting.ClockTime = 13

-- --- The level ---------------------------------------------------------------
--
-- `#` ground (grass where open sky is above it), `B` brick, `S` stone, `o` a
-- coin, `P` where the hero starts and `F` the flag. The bottom row is y = 0.

local Level = {
    "..............................................................................",
    "..............................................................................",
    "..............................................................o.o.............",
    "...........................................................SSSSSSSS...........",
    "..........................ooo.................................................",
    "........................BBBBBBB...................ooo.........................",
    ".................o.o...........................BBBBBBB................o.o..F...",
    "...............BBBBBB.....................................o......##########....",
    "..........................................o.o.......................#.#.#.....",
    "......o.o.o.............................SSSSSSS...........S.........#####.....",
    "..P..........................o.o.o...............................S####.........",
    "#############.....####################.......##########....##############....##",
    "#############.....####################.......##########....##############....##",
    "#############.....####################.......##########....##############....##",
}

local Height = #Level

local tiles = Instance.new("Tilemap2D")
tiles.Name = "Level"
tiles.Tileset = "asset://sprites/tiles.png" :: Content
tiles.TileSize = Vector2.new(16, 16)
tiles.Friction = 0
tiles.Parent = workspace

local function charAt(column: number, row: number): string
    local line = Level[row]
    if line == nil or column < 1 or column > #line then
        return "."
    end
    return string.sub(line, column, column)
end

local Spawn = Vector2.new(2.5, 4)
local coinSpots: { Vector2 } = {}
local flagSpot = Vector2.new(0, 0)

for row = 1, Height do
    local y = Height - row
    for column = 1, #Level[row] do
        local char = charAt(column, row)
        local x = column - 1
        if char == "#" then
            tiles:SetCell(x, y, if charAt(column, row - 1) == "#" then 2 else 1)
        elseif char == "B" then
            tiles:SetCell(x, y, 3)
        elseif char == "S" then
            tiles:SetCell(x, y, 4)
        elseif char == "o" then
            table.insert(coinSpots, Vector2.new(x + 0.5, y + 0.5))
        elseif char == "P" then
            Spawn = Vector2.new(x + 0.5, y + 0.6)
        elseif char == "F" then
            flagSpot = Vector2.new(x + 0.5, y + 0.5)
        end
    end
end

-- A wall of stone either side, so nobody walks off the map.
tiles:FillRect(-1, 0, -1, Height + 4, 4)
tiles:FillRect(#Level[1], 0, #Level[1], Height + 4, 4)

-- --- The hero ----------------------------------------------------------------

local hero = Instance.new("Part2D")
hero.Name = "Hero"
hero.Size = Vector2.new(0.8, 0.95)
hero.Image = "asset://sprites/hero.png" :: Content
hero.ImageRectSize = Vector2.new(16, 16)
hero.Filter = Enum.TextureFilter.Nearest
hero.FixedRotation = true
-- Zero, so the hero does not stick to a wall it presses against; the
-- controller brakes by itself.
hero.Friction = 0
hero.GravityScale = 3
hero.ZIndex = 5
hero.Position = Spawn
hero.Parent = workspace

-- --- Coins and the flag ------------------------------------------------------

local collected = 0
local finished = false

local function coinAt(spot: Vector2): Part2D
    local coin = Instance.new("Part2D")
    coin.Name = "Coin"
    coin.Shape = Enum.Shape2D.Circle
    coin.Size = Vector2.new(0.6, 0.6)
    coin.Image = "asset://sprites/coin.png" :: Content
    coin.Filter = Enum.TextureFilter.Nearest
    coin.Sensor = true
    coin.Anchored = true
    coin.ZIndex = 3
    coin.Position = spot
    coin.Parent = workspace
    return coin
end

local coins: { Part2D } = {}
for _, spot in coinSpots do
    table.insert(coins, coinAt(spot))
end

local flag = Instance.new("Part2D")
flag.Name = "Flag"
flag.Size = Vector2.new(1, 1)
flag.Image = "asset://sprites/flag.png" :: Content
flag.Filter = Enum.TextureFilter.Nearest
flag.Sensor = true
flag.Anchored = true
flag.ZIndex = 2
flag.Position = flagSpot
flag.Parent = workspace

-- --- The HUD -----------------------------------------------------------------

local screen = Instance.new("ScreenGui")
screen.Name = "Platformer"
screen.Parent = UIService

local status = Instance.new("TextLabel")
status.Name = "Status"
status.Size = UDim2.new(0, 360, 0, 36)
status.Position = UDim2.new(0, 16, 0, 12)
status.TextSize = 20
status.TextColor = Color3.fromRGB(255, 255, 255)
status.HorizontalAlignment = Enum.HorizontalAlignment.Left
status.BackgroundColor = Color3.fromRGB(20, 24, 36)
status.BackgroundTransparency = 0.4
status.Parent = screen

local function showStatus()
    if finished then
        status.Text = `Level complete -- {collected} / {#coins} coins`
    else
        status.Text = `Coins {collected} / {#coins}    A/D run, Space jump`
    end
end
showStatus()

for _, coin in coins do
    coin.Touched:Connect(function(other: Instance)
        if rawequal(other, hero) and coin.Parent ~= nil then
            coin:Destroy()
            collected += 1
            showStatus()
        end
    end)
end

flag.Touched:Connect(function(other: Instance)
    if rawequal(other, hero) and not finished then
        finished = true
        showStatus()
    end
end)

-- --- Input -------------------------------------------------------------------

local Gameplay = Instance.new("InputContext")
Gameplay.Name = "Gameplay"
Gameplay.Parent = workspace

local MoveAction = Instance.new("InputAction")
MoveAction.Name = "Move"
MoveAction.Type = Enum.InputActionType.Direction2D
MoveAction.Parent = Gameplay

local MoveKeys = Instance.new("InputBinding")
MoveKeys.Left = Enum.KeyCode.A
MoveKeys.Right = Enum.KeyCode.D
MoveKeys.Up = Enum.KeyCode.W
MoveKeys.Down = Enum.KeyCode.S
MoveKeys.Parent = MoveAction

local MoveArrows = Instance.new("InputBinding")
MoveArrows.Left = Enum.KeyCode.Left
MoveArrows.Right = Enum.KeyCode.Right
MoveArrows.Up = Enum.KeyCode.Up
MoveArrows.Down = Enum.KeyCode.Down
MoveArrows.Parent = MoveAction

local MoveStick = Instance.new("InputBinding")
MoveStick.KeyCode = Enum.KeyCode.LeftThumbstick
MoveStick.Parent = MoveAction

local JumpAction = Instance.new("InputAction")
JumpAction.Name = "Jump"
JumpAction.Parent = Gameplay

local JumpKey = Instance.new("InputBinding")
JumpKey.KeyCode = Enum.KeyCode.Space
JumpKey.Parent = JumpAction

local JumpPad = Instance.new("InputBinding")
JumpPad.KeyCode = Enum.KeyCode.ButtonSouth
JumpPad.Parent = JumpAction

Gameplay.Enabled = true
local _ = InputService

-- --- The controller ----------------------------------------------------------

local RunSpeed = 7
local GroundAcceleration = 70
local AirAcceleration = 40
-- Three tiles high at `GravityScale` 3: v = sqrt(2 * g * h).
local JumpSpeed = math.sqrt(2 * 9.81 * 3 * 3.2)
local CoyoteTime = 0.1
local JumpBuffer = 0.12

local ignoreHero = RaycastParams.new({ Filter = { hero :: Instance } })

local sinceGrounded = math.huge
local sinceJumpPressed = math.huge
local jumpWasDown = false
local jumping = false
local stride = 0

-- Standing is a short ray down from each side of the feet: one ray in the
-- middle would say "falling" at the edge of every ledge.
local function grounded(): boolean
    local position = hero.Position
    local half = hero.Size.X * 0.4
    local reach = hero.Size.Y * 0.5 + 0.08
    for _, offset in { -half, 0, half } do
        local hit =
            workspace:Raycast2D(Vector2.new(position.X + offset, position.Y), Vector2.new(0, -reach), ignoreHero)
        if hit ~= nil and hit.Normal.Y > 0.5 then
            return true
        end
    end
    return false
end

local function approach(value: number, target: number, step: number): number
    if value < target then
        return math.min(value + step, target)
    end
    return math.max(value - step, target)
end

RunService.PreSimulation:Connect(function(dt: number)
    local axis = MoveAction:GetState() :: Vector2
    local jumpDown = JumpAction:GetState() :: boolean
    local onGround = grounded()

    sinceGrounded = if onGround then 0 else sinceGrounded + dt
    if jumpDown and not jumpWasDown then
        sinceJumpPressed = 0
    else
        sinceJumpPressed += dt
    end
    jumpWasDown = jumpDown

    local velocity = hero.Velocity
    local target = if finished then 0 else axis.X * RunSpeed
    local vx = approach(velocity.X, target, (if onGround then GroundAcceleration else AirAcceleration) * dt)
    local vy = velocity.Y

    if onGround and vy <= 0 then
        jumping = false
    end
    if not finished and sinceJumpPressed <= JumpBuffer and sinceGrounded <= CoyoteTime then
        vy = JumpSpeed
        jumping = true
        sinceJumpPressed = math.huge
        sinceGrounded = math.huge
    end
    -- Let go early and the rise is cut: a tap is a hop, a hold is a leap.
    if jumping and not jumpDown and vy > 0 then
        vy *= 0.5
        jumping = false
    end
    hero.Velocity = Vector2.new(vx, vy)

    -- Facing and a two-frame stride, by distance run rather than by time.
    if math.abs(vx) > 0.1 then
        hero.FlipX = vx < 0
        stride += math.abs(vx) * dt
    else
        stride = 0
    end
    local frame = if onGround and math.floor(stride / 0.45) % 2 == 1 then 1 else 0
    hero.ImageRectOffset = Vector2.new(frame * 16, 0)

    -- Fell into a pit: back to the start.
    if hero.Position.Y < -6 then
        hero.Position = Spawn
        hero.Velocity = Vector2.zero
    end
end)

-- --- The camera --------------------------------------------------------------

local camera = Instance.new("Camera")
camera.Name = "Follow"
camera.Projection = Enum.CameraProjection.Orthographic
camera.OrthographicSize = 6
camera.Parent = workspace
workspace.CurrentCamera = camera

-- The view's half-width at 16:9; the camera stops where the level does, so
-- the walls at either end are the edge of the world rather than a view past it.
local HalfWidth = camera.OrthographicSize * 16 / 9
local LeftmostFocus = HalfWidth
local RightmostFocus = #Level[1] - HalfWidth

local focus = Vector2.new(LeftmostFocus, 6)
local function placeCamera(dt: number)
    -- Ahead of where the hero is running, eased, and never below the ground.
    local velocity = hero.Velocity
    local goal = Vector2.new(hero.Position.X + math.clamp(velocity.X * 0.4, -3, 3), math.max(hero.Position.Y + 1.5, 6))
    goal = Vector2.new(math.clamp(goal.X, LeftmostFocus, RightmostFocus), goal.Y)
    local t = 1 - math.exp(-6 * dt)
    focus = focus + (goal - focus) * t
    camera.CFrame = CFrame.new(focus.X, focus.Y, 50)
end
placeCamera(1)

RunService.Heartbeat:Connect(placeCamera)
local _ = PhysicsService
