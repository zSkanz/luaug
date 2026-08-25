--!strict
--[[
	The entry script.

	Every `.luau` under `src/scripts` becomes a `Script` under `ScriptService`
	at boot, in path-sorted order, and each runs on its own coroutine. Modules
	live elsewhere -- `src/shared` by convention -- and are reached by `require`
	rather than by being in the tree.

	**The world is in `content/scenes/main.scene.json` and the behaviour is
	here.** That split is the engine's whole stance on the two (ADR 0047): a
	scene is what a project STARTS with, a script is what it then does, and a
	project that builds its world in code has two answers to one question.

	It is also what makes the editor work. `luaug edit` mounts this script and
	does not run it until you press play (ADR 0058), so a part made at file
	scope would not exist while you were editing -- and one made on play would
	still be there, unauthored, the next time you saved the scene.
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

-- Authored in the scene, found here. `WaitForChild` rather than `FindFirstChild`
-- because a script should say what it needs rather than check whether it got it.
--
-- `Level` is a `Model` with `StreamingMode = "Persistent"`, which keeps it out of
-- the streaming grid entirely (ADR 0053). A world this size has no business
-- streaming, and a partitioned world only materialises around a focus somebody
-- registers -- so without that word these two parts would be in cells nothing
-- ever loads, and this line would wait forever.
local level = workspace:WaitForChild("Level") :: Model
local spinner = level:WaitForChild("Spinner") :: Part

-- Save this file with `luaug dev` running and watch the speed change without
-- restarting anything. That loop is what M3 of the engine exists to provide.
local RadiansPerSecond = 1.5

local origin = spinner.CFrame.Position
local elapsed = 0
RunService.Heartbeat:Connect(function(delta: number)
    elapsed += delta
    spinner.CFrame = CFrame.new(origin) * CFrame.fromEuler(0, elapsed * RadiansPerSecond, 0)
end)
