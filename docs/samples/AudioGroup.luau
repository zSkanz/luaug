--!strict
local AudioService = game:GetService("AudioService")

local music = Instance.new("AudioGroup")
music.Name = "Music"
music.Volume = 0.6 -- a multiplier, not decibels
music.Parent = AudioService

local track = Instance.new("Sound")
track.Content = "asset://audio/theme.ogg"
track.Looped = true
track.Group = music -- Volume * group.Volume * MasterVolume
track.Parent = AudioService -- not a BasePart, so it is 2D
track:Play()
