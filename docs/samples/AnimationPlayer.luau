--!strict
local character = Instance.new("MeshPart")
character.MeshContent = "asset://models/hero.glb"
character.Parent = workspace

local player = Instance.new("AnimationPlayer")
player.Parent = character -- the skinned MeshPart whose clips these are

local walk = player:LoadAnimation("Walk")
if walk.Length > 0 then -- zero until the mesh has been read
    walk.Looped = true
    walk:Play(0.2) -- fade the weight in over 0.2 seconds
end
