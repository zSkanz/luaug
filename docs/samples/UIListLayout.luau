--!strict
local menu = Instance.new("Frame")
menu.Size = UDim2.new(0, 360, 0, 220)
menu.Position = UDim2.fromScale(0.5, 0.5)
menu.AnchorPoint = Vector2.new(0.5, 0.5)

-- A modifier, parented BESIDE what it arranges rather than containing it.
local layout = Instance.new("UIListLayout")
layout.FillDirection = Enum.FillDirection.Vertical
layout.Padding = UDim.new(0, 12)
layout.HorizontalAlignment = Enum.HorizontalAlignment.Center
layout.SortOrder = Enum.SortOrder.LayoutOrder
layout.Parent = menu
