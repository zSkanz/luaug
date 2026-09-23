--!strict
--- Conformance: `ParticleEmitter` (F2), from a script.
---
--- **The particles themselves are not observable** -- they are a picture, and no
--- script can find one -- so what is pinned here is everything a script CAN
--- see: the defaults a new emitter has, what each property refuses, and that a
--- burst is a verb that works whether or not the emitter streams.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("ParticleEmitter", function()
    it("starts as a soft white stream that fades out as it dies", function()
        local emitter = Instance.new("ParticleEmitter")
        expect(emitter.Enabled):ToBe(true)
        expect(emitter.Rate):ToBe(20)
        expect(emitter.Lifetime):ToBe(2)
        expect(emitter.Speed):ToBe(4)
        expect(emitter.SpreadAngle):ToBe(15)
        expect(emitter.Acceleration):ToBe(vector.zero)
        expect(emitter.Drag):ToBe(0)
        expect(emitter.Color):ToBe(Color3.new(1, 1, 1))
        expect(emitter.ColorEnd):ToBe(Color3.new(1, 1, 1))
        expect(emitter.Size):ToBe(0.5)
        expect(emitter.SizeEnd):ToBe(0.5)
        expect(emitter.Transparency):ToBe(0)
        expect(emitter.TransparencyEnd):ToBe(1)
        expect(emitter.LightEmission):ToBe(0)
        expect(emitter.Brightness):ToBe(1)
        expect(emitter.Shape):ToBe(Enum.ParticleShape.Soft)
        emitter:Destroy()
    end)

    it("round-trips what it accepts", function()
        local emitter = Instance.new("ParticleEmitter")
        emitter.Rate = 250
        emitter.Acceleration = vector.create(0, -9.81, 0)
        emitter.ColorEnd = Color3.new(1, 0, 0)
        emitter.LightEmission = 1
        emitter.Shape = Enum.ParticleShape.Disc
        emitter.SpreadAngle = 180
        expect(emitter.Rate):ToBe(250)
        expect(emitter.Acceleration.y):ToBeCloseTo(-9.81, 1e-4)
        expect(emitter.ColorEnd):ToBe(Color3.new(1, 0, 0))
        expect(emitter.LightEmission):ToBe(1)
        expect(emitter.Shape):ToBe(Enum.ParticleShape.Disc)
        expect(emitter.SpreadAngle):ToBe(180)
        emitter:Destroy()
    end)

    it("refuses a negative rate, a fraction past one and a cone past a sphere", function()
        local emitter = Instance.new("ParticleEmitter") :: any
        expect(function()
            emitter.Rate = -1
        end):ToThrow()
        expect(function()
            emitter.Transparency = 1.5
        end):ToThrow()
        expect(function()
            emitter.LightEmission = 2
        end):ToThrow()
        expect(function()
            emitter.SpreadAngle = 181
        end):ToThrow()
        expect(function()
            emitter.Shape = Enum.PartShape.Block
        end):ToThrow()
        emitter:Destroy()
    end)

    it("emits a burst whether or not it streams", function()
        local part = Instance.new("Part")
        part.Parent = workspace
        local emitter = Instance.new("ParticleEmitter")
        emitter.Enabled = false
        emitter.Parent = part
        -- A verb, not a property: it returns nothing and raises nothing, and
        -- a count past the cap is clamped rather than refused.
        emitter:Emit(10)
        emitter:Emit(5000)
        emitter:Emit(0)
        part:Destroy()
    end)
end)
