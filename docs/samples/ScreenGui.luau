--!strict
local UIService = game:GetService("UIService")

local screen = Instance.new("ScreenGui")
screen.Name = "Hud"
screen.Parent = UIService

local bar = Instance.new("Frame")
bar.Size = UDim2.new(1, 0, 0, 44)
bar.BackgroundColor = Color3.fromRGB(18, 20, 28)
bar.BackgroundTransparency = 0.35
bar.Parent = screen
