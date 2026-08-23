--!strict
local RunService = game:GetService("RunService")

local character = Instance.new("CharacterBody")
character.Size = vector.create(2, 5, 2)
character.WalkSpeed = 7
character.JumpSpeed = 5.5
character.AutoStepHeight = 0.6
character.Position = vector.create(0, 6, 0)
character.Parent = workspace

RunService.Heartbeat:Connect(function(dt: number)
    -- Intent for one tick: a character told nothing stops.
    character:Move(vector.create(0, 0, -1))
    if character.Grounded then
        character:Jump()
    end
end)
