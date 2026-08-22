--!strict
--[[
	The first LuauG project whose world is DATA (ADR 0047).

	Every example before this one builds its world in code: `init.luau` runs at
	boot and calls `Instance.new` until there is something to look at. This one
	does not. Its world is `main.scene.json`, the editor authors that file, and
	this script is only what the world DOES.

	That is the arrangement Unity, Unreal and Roblox all use, and it is what
	makes edit → test → save a loop: a change you make in the editor has
	somewhere to live that running the game does not overwrite.

	**Open it with `luaug edit examples/06-scene`**, move something, press play,
	press stop, press save. Then close the editor and open it again.
]]

local RunService = game:GetService("RunService")
local TagService = game:GetService("TagService")

--- How fast the marked parts turn, in radians per second. A behaviour, not a
--- world: which parts turn is the scene's business and this script only asks
--- for the ones wearing the tag.
local SpinRate = 1.2

RunService.Heartbeat:Connect(function(dt: number)
    for _, instance in TagService:GetTagged("Spin") do
        if instance:IsA("BasePart") then
            local part = instance :: BasePart
            part.CFrame = part.CFrame * CFrame.fromEuler(0, SpinRate * dt, 0)
        end
    end
end)

print(`06-scene: the world came from a file, and {#TagService:GetTagged("Spin")} part(s) are asking to spin.`)
