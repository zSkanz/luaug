--!strict
--- Navigation (ADR 0089): a character that finds its own way through a maze.
---
--- The maze is the ASCII map below, built from anchored parts -- which is all
--- walkable ground is. `NavigationService:FindPath` answers the corners to walk
--- to; walking them is this script's job, one `CharacterBody:Move` a tick. When
--- the walker arrives, a new goal is drawn from a seeded `Random`, so every run
--- is the same walk.
---
--- Press G to move the goal to a new spot mid-walk; the path is asked for
--- again. Press B to drop a wall block in front of the walker and watch it go
--- round -- the navigation sees a new wall on the next query.

local InputService = game:GetService("InputService")
local Lighting = game:GetService("Lighting")
local NavigationService = game:GetService("NavigationService")
local RunService = game:GetService("RunService")

Lighting.ClockTime = 14

-- `#` a wall, `.` floor, `S` where the walker starts. One cell is 3 metres.
local Maze = {
    "####################",
    "#S.....#...........#",
    "#.####.#.#####.###.#",
    "#.#....#.....#...#.#",
    "#.#.########.###.#.#",
    "#.#..........#...#.#",
    "#.######.###.#.###.#",
    "#......#...#.#.....#",
    "######.###.#.#####.#",
    "#......#...#.......#",
    "#.######.#########.#",
    "#........#.........#",
    "####################",
}
local Cell = 3
local WallHeight = 3

local function cellCentre(column: number, row: number): vector
    return vector.create((column - 0.5) * Cell, 0, (row - 0.5) * Cell)
end

local floor = Instance.new("Part")
floor.Name = "Floor"
floor.Size = vector.create(#Maze[1] * Cell, 1, #Maze * Cell)
floor.Position = vector.create(#Maze[1] * Cell / 2, -0.5, #Maze * Cell / 2)
floor.Color = Color3.fromRGB(196, 190, 176)
floor.Anchored = true
floor.Parent = workspace

local start = cellCentre(2, 2)
local open: { vector } = {}
for row = 1, #Maze do
    for column = 1, #Maze[row] do
        local char = string.sub(Maze[row], column, column)
        if char == "#" then
            local wall = Instance.new("Part")
            wall.Name = "Wall"
            wall.Size = vector.create(Cell, WallHeight, Cell)
            wall.Position = cellCentre(column, row) + vector.create(0, WallHeight / 2, 0)
            wall.Color = Color3.fromRGB(70, 84, 110)
            wall.Anchored = true
            wall.Parent = workspace
        else
            table.insert(open, cellCentre(column, row))
            if char == "S" then
                start = cellCentre(column, row)
            end
        end
    end
end

-- --- The walker and its goal ------------------------------------------------

local walker = Instance.new("CharacterBody")
walker.Name = "Walker"
walker.Size = vector.create(1, 2, 1)
walker.Position = start + vector.create(0, 1.1, 0)
walker.Color = Color3.fromRGB(230, 120, 60)
walker.WalkSpeed = 7
walker.Parent = workspace

local goal = Instance.new("Part")
goal.Name = "Goal"
goal.Shape = Enum.PartShape.Cylinder
goal.Size = vector.create(0.2, 2.2, 2.2)
goal.Color = Color3.fromRGB(90, 220, 120)
goal.Anchored = true
goal.CanCollide = false
goal.Parent = workspace

-- Breadcrumbs: one small marker at each corner of the current path.
local crumbs = Instance.new("Folder")
crumbs.Name = "Path"
crumbs.Parent = workspace

local random = Random.new(20260924)
local waypoints: { vector } = {}
local nextPoint = 1
local reached = 0

local function showPath()
    for _, crumb in crumbs:GetChildren() do
        crumb:Destroy()
    end
    for index, point in waypoints do
        local crumb = Instance.new("Part")
        crumb.Name = `Corner{index}`
        crumb.Size = vector.create(0.4, 0.1, 0.4)
        crumb.Position = point + vector.create(0, 0.05, 0)
        crumb.Color = Color3.fromRGB(250, 220, 90)
        crumb.Anchored = true
        crumb.CanCollide = false
        crumb.Parent = crumbs
    end
end

local function plan()
    local from = walker.Position - vector.create(0, walker.Size.y / 2, 0)
    local points, complete = NavigationService:FindPath(from, goal.Position)
    waypoints = points or {}
    -- The first corner is where the walker already stands.
    nextPoint = if #waypoints > 1 then 2 else 1
    showPath()
    if not complete then
        print(`[navigation] no whole path to the goal; walking as far as it goes`)
    end
end

local function newGoal()
    local spot = open[random:NextInteger(1, #open)]
    goal.CFrame = CFrame.new(spot.x, 0.1, spot.z) * CFrame.fromEuler(0, 0, math.pi / 2)
    plan()
end

-- --- Walking ----------------------------------------------------------------

RunService.PreSimulation:Connect(function()
    if nextPoint > #waypoints then
        walker:Move(vector.zero)
        return
    end
    local target = waypoints[nextPoint]
    local position = walker.Position
    local offset = vector.create(target.x - position.x, 0, target.z - position.z)
    local distance = vector.magnitude(offset)
    if distance < 0.35 then
        nextPoint += 1
        if nextPoint > #waypoints then
            reached += 1
            print(`[navigation] reached goal {reached}`)
            newGoal()
        end
        return
    end
    walker:Move(offset / distance)
end)

-- --- Controls ---------------------------------------------------------------

InputService.InputBegan:Connect(function(input: InputObject, consumed: boolean)
    if consumed then
        return
    end
    if input.KeyCode == Enum.KeyCode.G then
        newGoal()
    elseif input.KeyCode == Enum.KeyCode.B then
        -- A block a few metres ahead, on the path: the next query goes round.
        if nextPoint <= #waypoints then
            local target = waypoints[nextPoint]
            local ahead = walker.Position + vector.normalize(target - walker.Position) * 2.5
            local block = Instance.new("Part")
            block.Name = "Dropped"
            block.Size = vector.create(Cell, WallHeight, Cell)
            block.Position = vector.create(ahead.x, WallHeight / 2, ahead.z)
            block.Color = Color3.fromRGB(160, 60, 60)
            block.Anchored = true
            block.Parent = workspace
            plan()
        end
    end
end)

-- --- The view ---------------------------------------------------------------

local camera = Instance.new("Camera")
camera.Name = "Overhead"
camera.FieldOfView = 50
camera.Parent = workspace
workspace.CurrentCamera = camera
local middle = vector.create(#Maze[1] * Cell / 2, 0, #Maze * Cell / 2)
camera.CFrame = CFrame.lookAt(middle + vector.create(0, 62, 26), middle)

newGoal()
