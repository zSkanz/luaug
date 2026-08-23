--!strict
local origin = vector.create(0, 6, 0)

local params = RaycastParams.new({
    FilterType = Enum.RaycastFilterType.Exclude,
})

-- The direction's LENGTH is the range: five metres straight down.
local result: RaycastResult? = workspace:Raycast(origin, vector.create(0, -5, 0), params)

if result ~= nil then
    print(result.Instance.Name, result.Distance)
end
