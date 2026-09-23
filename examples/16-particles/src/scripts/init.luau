--!strict
--[[
	Sparks, smoke, fire and water (F2).

	Four emitters and a burst. Each is one `ParticleEmitter` parented to a part
	or an attachment, and everything it does follows from its properties: where
	it points (its parent's up), how fast and how widely it throws, what pulls
	on what it threw, and what a particle looks like from birth to death.

	Particles are a picture, not the world -- they collide with nothing and a
	script cannot find one -- which is why a fire of hundreds costs one draw.
]]

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

Lighting.ClockTime = 19.2

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Anchored = true
ground.Size = vector.create(40, 1, 40)
ground.Position = vector.create(0, -0.5, 0)
ground.Color = Color3.fromRGB(70, 74, 68)
ground.Parent = Workspace

local function block(name: string, at: vector, size: vector, color: Color3): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Anchored = true
    part.Size = size
    part.Position = at
    part.Color = color
    part.Parent = Workspace
    return part
end

-- **A campfire**: a flat log pile that burns all over, because a part is a
-- volume particles are born anywhere inside. Additive, so the flames brighten
-- what is behind them and never darken it.
local logs = block("Logs", vector.create(-6, 0.25, 0), vector.create(1.6, 0.5, 1.6), Color3.fromRGB(60, 38, 22))
local fire = Instance.new("ParticleEmitter")
fire.Name = "Fire"
fire.Rate = 160
fire.Lifetime = 0.8
fire.Speed = 1.4
fire.SpreadAngle = 8
fire.Acceleration = vector.create(0, 4, 0)
fire.Color = Color3.new(1, 0.6, 0.15)
fire.ColorEnd = Color3.new(0.8, 0.1, 0.02)
fire.Size = 0.7
fire.SizeEnd = 0.1
fire.LightEmission = 1
fire.Brightness = 3
fire.Parent = logs

-- **Its smoke**, from the same logs: blended rather than added, grey, slowing
-- as it rises and swelling as it thins.
local smoke = Instance.new("ParticleEmitter")
smoke.Name = "Smoke"
smoke.Rate = 12
smoke.Lifetime = 4
smoke.Speed = 1.5
smoke.SpreadAngle = 20
smoke.Acceleration = vector.create(0.4, 0.6, 0)
smoke.Drag = 0.3
smoke.Color = Color3.new(0.35, 0.35, 0.36)
smoke.ColorEnd = Color3.new(0.55, 0.55, 0.56)
smoke.Size = 0.8
smoke.SizeEnd = 3
smoke.Transparency = 0.4
smoke.Parent = logs

-- **Sparks from an anvil**: an attachment is a point and a direction, so this
-- aims the jet up and to the side. Small crisp discs, bright enough to bloom,
-- falling back under gravity.
local anvil = block("Anvil", vector.create(0, 0.6, 0), vector.create(1.4, 1.2, 0.8), Color3.fromRGB(58, 60, 66))
local muzzle = Instance.new("Attachment")
muzzle.Name = "Muzzle"
muzzle.CFrame = CFrame.new(0, 0.6, 0) * CFrame.fromEuler(0, 0, -0.5)
muzzle.Parent = anvil
local sparks = Instance.new("ParticleEmitter")
sparks.Name = "Sparks"
sparks.Rate = 60
sparks.Lifetime = 1.2
sparks.Speed = 7
sparks.SpreadAngle = 25
sparks.Acceleration = vector.create(0, -12, 0)
sparks.Color = Color3.new(1, 0.85, 0.4)
sparks.ColorEnd = Color3.new(1, 0.3, 0.05)
sparks.Size = 0.12
sparks.SizeEnd = 0.05
sparks.Transparency = 0
sparks.TransparencyEnd = 0.2
sparks.LightEmission = 1
sparks.Brightness = 6
sparks.Shape = Enum.ParticleShape.Disc
sparks.Parent = muzzle

-- **A fountain**: blue discs thrown up in a cone and brought down again.
local basin = block("Basin", vector.create(6, 0.2, 0), vector.create(2.4, 0.4, 2.4), Color3.fromRGB(120, 124, 130))
local water = Instance.new("ParticleEmitter")
water.Name = "Water"
water.Rate = 120
water.Lifetime = 1.6
water.Speed = 8
water.SpreadAngle = 8
water.Acceleration = vector.create(0, -9.81, 0)
water.Color = Color3.new(0.45, 0.7, 1)
water.ColorEnd = Color3.new(0.8, 0.9, 1)
water.Size = 0.18
water.SizeEnd = 0.3
water.Transparency = 0.1
water.TransparencyEnd = 0.8
water.Shape = Enum.ParticleShape.Disc
water.Parent = basin

-- **A burst every two seconds**: `Emit` works whether or not the emitter
-- streams, because an explosion is a burst and not a stream.
local charge = block("Charge", vector.create(0, 0.25, -6), vector.create(0.5, 0.5, 0.5), Color3.fromRGB(200, 60, 40))
local blast = Instance.new("ParticleEmitter")
blast.Name = "Blast"
blast.Enabled = false
blast.Lifetime = 1
blast.Speed = 9
blast.SpreadAngle = 70
blast.Drag = 1.5
blast.Acceleration = vector.create(0, -3, 0)
blast.Color = Color3.new(1, 0.8, 0.3)
blast.ColorEnd = Color3.new(0.6, 0.1, 0.05)
blast.Size = 0.6
blast.SizeEnd = 1.4
blast.LightEmission = 0.7
blast.Brightness = 2.5
blast.Parent = charge

local elapsed = 0
RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt
    if elapsed >= 2 then
        elapsed -= 2
        blast:Emit(120)
    end
end)
blast:Emit(120)

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(vector.create(0, 6, 15), vector.create(0, 2, 0))
camera.Parent = Workspace
Workspace.CurrentCamera = camera
