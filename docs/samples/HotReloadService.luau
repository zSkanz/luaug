--!strict
local HotReloadService = game:GetService("HotReloadService")

local at = vector.create(0, 5, 0)

-- SaveState takes nil, booleans, numbers, strings, buffers and tables of those.
-- A vector does not survive: save three numbers.
HotReloadService.PreReload:Connect(function()
    HotReloadService:SaveState("spawn", { at.x, at.y, at.z })
end)
