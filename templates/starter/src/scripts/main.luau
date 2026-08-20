--!strict
--[[
	The entry script.

	Every `.luau` under `src/scripts` becomes a `Script` under `ScriptService`
	at boot, in path-sorted order, and each runs on its own coroutine. Modules
	live elsewhere -- `src/shared` by convention -- and are reached by `require`
	rather than by being in the tree.
]]

local greeting = require("@shared/greeting")

local HotReloadService = game:GetService("HotReloadService")
local RunService = game:GetService("RunService")

print(greeting.forProject())

-- `luaug dev` rebuilds this world every time you save. `IsReload` is how a
-- script tells the two apart, and anything tagged "PreserveOnReload" is already
-- back in the tree by the time this line runs.
if HotReloadService:IsReload() then
    print(greeting.afterReload())
end

local spinner = Instance.new("Part")
spinner.Name = "Spinner"
spinner.Position = vector.create(0, 2, 0)
spinner.Size = vector.create(2, 2, 2)
spinner.Color = Color3.fromRGB(120, 190, 255)
spinner.Parent = workspace

-- Save this file with `luaug dev` running and watch the speed change without
-- restarting anything. That loop is what M3 of the engine exists to provide.
local RadiansPerSecond = 1.5

local elapsed = 0
RunService.Heartbeat:Connect(function(delta: number)
    elapsed += delta
    spinner.CFrame = CFrame.new(vector.create(0, 2, 0)) * CFrame.fromEuler(0, elapsed * RadiansPerSecond, 0)
end)
