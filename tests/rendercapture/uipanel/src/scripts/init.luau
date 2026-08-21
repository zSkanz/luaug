--!strict

-- The M6 UI gate scene: a HUD whose every element proves one layout rule.
--
-- **Every number is a literal and nothing moves**, for the same reason M4's
-- capture scene is written that way: a golden that depended on elapsed time
-- would depend on how many ticks had passed when it was recorded, and a
-- one-tick shift changes every number in a command stream.
--
-- The point of the scene is the pair of goldens taken from it. The SAME script
-- is captured at two resolutions, and the two streams have to differ in exactly
-- the way scaling predicts: an element sized in `Scale` doubles with the window
-- and an element sized in `Offset` does not. A layout that ignored the window
-- would produce two identical streams, which is the failure this is built to
-- notice.

local UIService = game:GetService("UIService")

local Panel = Color3.fromRGB(28, 32, 44)
local Accent = Color3.fromRGB(96, 176, 255)
local Warm = Color3.fromRGB(240, 176, 96)
local Ink = Color3.fromRGB(236, 240, 248)

local screen = Instance.new("ScreenGui")
screen.Name = "Hud"
screen.Parent = UIService

--- A half-width panel pinned to the left. Sized in SCALE, so it is the element
--- that has to change between the two goldens.
local sidebar = Instance.new("Frame")
sidebar.Name = "Sidebar"
sidebar.Size = UDim2.new(0.35, 0, 1, 0)
sidebar.BackgroundColor = Panel
sidebar.Parent = screen

local sidebarPadding = Instance.new("UIPadding")
sidebarPadding.PaddingTop = UDim.new(0, 16)
sidebarPadding.PaddingLeft = UDim.new(0, 16)
sidebarPadding.PaddingRight = UDim.new(0, 16)
sidebarPadding.Parent = sidebar

local list = Instance.new("UIListLayout")
list.FillDirection = Enum.FillDirection.Vertical
list.Padding = UDim.new(0, 10)
list.Parent = sidebar

--- Three rows in a list. Each is sized in OFFSET on the vertical axis, so their
--- heights are the same in both goldens while their width follows the panel.
for index = 1, 3 do
    local row = Instance.new("TextLabel")
    row.Name = `Row{index}`
    row.LayoutOrder = index
    row.Size = UDim2.new(1, 0, 0, 34)
    row.BackgroundColor = index == 2 and Accent or Color3.fromRGB(44, 50, 66)
    row.Text = `Row {index}`
    row.TextColor = Ink
    row.TextSize = 16
    row.HorizontalAlignment = Enum.HorizontalAlignment.Left
    row.Parent = sidebar
end

--- A badge anchored to the middle of the window by a fraction with no name --
--- the case ADR 0040 says a flow solver could not place.
local badge = Instance.new("Frame")
badge.Name = "Badge"
badge.AnchorPoint = Vector2.new(0.3, 0.7)
badge.Position = UDim2.fromScale(0.72, 0.5)
badge.Size = UDim2.fromOffset(160, 90)
badge.BackgroundColor = Warm
badge.BackgroundTransparency = 0.15
badge.Parent = screen

local badgeText = Instance.new("TextLabel")
badgeText.Size = UDim2.fromScale(1, 1)
badgeText.BackgroundTransparency = 1
badgeText.Text = "ANCHORED"
badgeText.TextColor = Color3.fromRGB(24, 20, 12)
badgeText.TextSize = 18
badgeText.Parent = badge

--- A clipped strip along the bottom, with a child that overhangs it. It is here
--- so a golden covers the scissor path: the overhanging half must not be drawn.
local strip = Instance.new("Frame")
strip.Name = "ClippedStrip"
strip.AnchorPoint = Vector2.new(0, 1)
strip.Position = UDim2.new(0.4, 0, 1, -12)
strip.Size = UDim2.new(0.55, 0, 0, 40)
strip.BackgroundColor = Color3.fromRGB(18, 20, 28)
strip.ClipsDescendants = true
strip.Parent = screen

local overhang = Instance.new("Frame")
overhang.Name = "Overhang"
overhang.Position = UDim2.new(0.6, 0, 0, 8)
overhang.Size = UDim2.new(0.8, 0, 0, 24)
overhang.BackgroundColor = Accent
overhang.Parent = strip
