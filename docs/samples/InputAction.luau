--!strict
local InputService = game:GetService("InputService")

local gameplay = Instance.new("InputContext")
gameplay.Name = "Gameplay"
gameplay.Parent = InputService

local jump = Instance.new("InputAction")
jump.Name = "Jump"
jump.Type = Enum.InputActionType.Bool
jump.Parent = gameplay

-- Two bindings, one action: neither the game nor the player cares which fired.
local key = Instance.new("InputBinding")
key.KeyCode = Enum.KeyCode.Space
key.Parent = jump

local pad = Instance.new("InputBinding")
pad.KeyCode = Enum.KeyCode.ButtonSouth
pad.Parent = jump

jump.Pressed:Connect(function()
    print("jump")
end)
