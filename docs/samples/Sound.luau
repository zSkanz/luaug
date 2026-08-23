--!strict
local speaker = Instance.new("Part")
speaker.Anchored = true
speaker.Parent = workspace

local sound = Instance.new("Sound")
sound.Content = "asset://audio/engine.ogg"
sound.Volume = 0.4
sound.Looped = true
sound.RollOffMinDistance = 6
sound.RollOffMaxDistance = 45
sound.Parent = speaker -- a BasePart, so it is positional
sound:Play()
