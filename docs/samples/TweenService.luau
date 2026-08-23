--!strict
local TweenService = game:GetService("TweenService")

local panel = Instance.new("Frame")
local info = TweenInfo.new(0.6, Enum.EasingStyle.Back, Enum.EasingDirection.Out)
local slide = TweenService:Create(panel, info, {
    Position = UDim2.new(0.5, 0, 0.5, 0),
})

slide.Completed:Connect(function(state: EnumPlaybackState)
    if state == Enum.PlaybackState.Completed then
        print("settled")
    end
end)

slide:Play()
