--!strict
--- `TweenService`, `TweenInfo` and `Tween`, from api-design.md §2.1 and §2.3.
---
--- A tween steps on the SimClock, so these cases advance time with `task.wait`
--- and read the property afterwards. That is not a workaround: it is the whole
--- claim being tested. A tween that ran on the render clock could not be
--- checked this way at all, and could not be replayed either.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local PhysicsService = game:GetService("PhysicsService")
local TweenService = game:GetService("TweenService")

--- One tick, expressed against `FixedTimestep` rather than as a literal, so the
--- suite stays correct at 30 Hz or 240 Hz (§3.2).
local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("TweenInfo", function()
    it("defaults to a one-second Quad-Out with no repeat, reverse or delay", function()
        local info = TweenInfo.new()
        expect(info.Time):ToBe(1)
        expect(info.EasingStyle):ToBe(Enum.EasingStyle.Quad)
        expect(info.EasingDirection):ToBe(Enum.EasingDirection.Out)
        expect(info.RepeatCount):ToBe(0)
        expect(info.Reverses):ToBe(false)
        expect(info.DelayTime):ToBe(0)
    end)

    it("round-trips every field", function()
        local info = TweenInfo.new(0.25, Enum.EasingStyle.Bounce, Enum.EasingDirection.InOut, 3, true, 0.5)
        expect(info.Time):ToBe(0.25)
        expect(info.EasingStyle):ToBe(Enum.EasingStyle.Bounce)
        expect(info.EasingDirection):ToBe(Enum.EasingDirection.InOut)
        expect(info.RepeatCount):ToBe(3)
        expect(info.Reverses):ToBe(true)
        expect(info.DelayTime):ToBe(0.5)
    end)

    it("is typeof TweenInfo, immutable, and compares field by field", function()
        expect(typeof(TweenInfo.new())):ToBe("TweenInfo")
        expect(TweenInfo.new(0.5)):ToBe(TweenInfo.new(0.5))
        expect(TweenInfo.new(0.5)).Never:ToBe(TweenInfo.new(0.6))
        expect(function()
            (TweenInfo.new() :: any).Time = 3
        end):ToThrow()
    end)

    it("refuses a negative time or delay and accepts a negative repeat count", function()
        -- A negative duration is a sign error in the caller's arithmetic, and a
        -- tween that silently took zero seconds would look like the property
        -- never animated. A negative repeat count is the one negative here that
        -- means something: repeat forever.
        expect(function()
            return TweenInfo.new(-1)
        end):ToThrow("script.err.tween_info_range")
        expect(function()
            return TweenInfo.new(1, Enum.EasingStyle.Linear, Enum.EasingDirection.Out, 0, false, -1)
        end):ToThrow("script.err.tween_info_range")
        expect(TweenInfo.new(1, Enum.EasingStyle.Linear, Enum.EasingDirection.Out, -1).RepeatCount):ToBe(-1)
    end)
end)

describe("TweenService:GetValue", function()
    local Fixture = require("./easing_reference")

    it("matches the reference tables for every style and direction", function()
        -- The fixture's numbers were computed from the published formulas by an
        -- implementation written separately from the engine's. Two
        -- implementations of one definition agreeing is evidence; a table
        -- generated from the engine would only certify the engine.
        for styleName, byDirection in Fixture.Values do
            for directionName, expected in byDirection do
                local style = (Enum.EasingStyle :: any)[styleName]
                local direction = (Enum.EasingDirection :: any)[directionName]
                for index, alpha in Fixture.Alphas do
                    local actual = TweenService:GetValue(alpha, style, direction)
                    expect(actual):ToBeCloseTo(expected[index], 1e-5)
                end
            end
        end
    end)

    it("is exactly 0 at 0 and exactly 1 at 1 for every curve", function()
        -- Three of the eleven reach the ends only because their definitions
        -- special-case it, so this is checked separately from the sampled table
        -- and with no tolerance at all.
        for _, style in Enum.EasingStyle:GetEnumItems() do
            for _, direction in Enum.EasingDirection:GetEnumItems() do
                expect(TweenService:GetValue(0, style, direction)):ToBe(0)
                expect(TweenService:GetValue(1, style, direction)):ToBe(1)
            end
        end
    end)

    it("does not clamp, because two of the curves overshoot on purpose", function()
        -- `Back` and `Elastic` leave 0-1, and that IS them. A clamp would turn
        -- both into slower versions of Quad.
        local backOut = TweenService:GetValue(0.4, Enum.EasingStyle.Back, Enum.EasingDirection.Out)
        expect(backOut > 1):ToBe(true)
    end)
end)

describe("Tween", function()
    local part: Part = nil :: any

    beforeEach(function()
        part = Instance.new("Part")
        part.Anchored = true
        part.Name = "TweenSpecPart"
        part.Parent = workspace
    end)

    afterEach(function()
        part:Destroy()
    end)

    it("is created stopped and writes nothing until it is played", function()
        local tween = TweenService:Create(part, TweenInfo.new(1), { Transparency = 1 })
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Begin)
        step(10)
        expect(part.Transparency):ToBe(0)
    end)

    it("reaches its goal EXACTLY, not approximately", function()
        -- A tween that stopped at 0.9999 of the way would leave a property
        -- somebody has to explain.
        local tween = TweenService:Create(part, TweenInfo.new(0.1, Enum.EasingStyle.Linear), { Transparency = 1 })
        tween:Play()
        step(12)
        expect(part.Transparency):ToBe(1)
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Completed)
    end)

    it("fires Completed with the state that ended it", function()
        local tween = TweenService:Create(part, TweenInfo.new(0.05, Enum.EasingStyle.Linear), { Transparency = 1 })
        local seen: EnumItem = nil :: any
        tween.Completed:Connect(function(state: EnumItem)
            seen = state
        end)
        tween:Play()
        step(8)
        expect(seen):ToBe(Enum.PlaybackState.Completed)
    end)

    it("moves through the values in between", function()
        local tween = TweenService:Create(part, TweenInfo.new(1, Enum.EasingStyle.Linear), { Transparency = 1 })
        tween:Play()
        step(15)
        -- A quarter of a linear second, give or take the tick the check lands
        -- on. A window rather than a moment, which is M5's lesson about
        -- checking a moving thing.
        expect(part.Transparency > 0.15):ToBe(true)
        expect(part.Transparency < 0.4):ToBe(true)
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Playing)
    end)

    it("holds the property still during its delay", function()
        local info = TweenInfo.new(0.1, Enum.EasingStyle.Linear, Enum.EasingDirection.Out, 0, false, 0.2)
        local tween = TweenService:Create(part, info, { Transparency = 1 })
        tween:Play()
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Delayed)
        step(6)
        expect(part.Transparency):ToBe(0)
        step(12)
        expect(part.Transparency):ToBe(1)
    end)

    it("pauses where it is and resumes from there", function()
        local tween = TweenService:Create(part, TweenInfo.new(1, Enum.EasingStyle.Linear), { Transparency = 1 })
        tween:Play()
        step(20)
        tween:Pause()
        local paused = part.Transparency
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Paused)
        step(20)
        expect(part.Transparency):ToBe(paused)
        tween:Play()
        step(10)
        expect(part.Transparency > paused):ToBe(true)
    end)

    it("cancels without undoing", function()
        -- Cancelling is not undoing. A tween that snapped back would be
        -- impossible to interrupt gracefully, which is the one thing Cancel is
        -- for.
        local tween = TweenService:Create(part, TweenInfo.new(1, Enum.EasingStyle.Linear), { Transparency = 1 })
        tween:Play()
        step(20)
        local reached = part.Transparency
        expect(reached > 0):ToBe(true)
        tween:Cancel()
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Cancelled)
        step(20)
        expect(part.Transparency):ToBe(reached)
    end)

    it("returns to its start when it reverses", function()
        local info = TweenInfo.new(0.1, Enum.EasingStyle.Linear, Enum.EasingDirection.Out, 0, true)
        local tween = TweenService:Create(part, info, { Transparency = 1 })
        tween:Play()
        step(20)
        expect(part.Transparency):ToBeCloseTo(0, 1e-5)
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Completed)
    end)

    it("reports the instance it animates, and nil once that is destroyed", function()
        local doomed = Instance.new("Part")
        doomed.Parent = workspace
        local tween = TweenService:Create(doomed, TweenInfo.new(1), { Transparency = 1 })
        expect(tween.Instance):ToBe(doomed)
        tween:Play()
        doomed:Destroy()
        step(2)
        -- A tween is a reference and not an ownership: it stops rather than
        -- raising, and rather than keeping the instance alive.
        expect(tween.Instance):ToBeNil()
        expect(tween.PlaybackState):ToBe(Enum.PlaybackState.Cancelled)
    end)

    it("writes through the same path an assignment does", function()
        -- The property-changed signal fires for a tweened write exactly as it
        -- does for `part.Transparency = x`. That is the point of not having a
        -- second write route: a tweened value and an assigned one MEAN the same
        -- thing.
        local fired = 0
        part:GetPropertyChangedSignal("Transparency"):Connect(function()
            fired += 1
        end)
        local tween = TweenService:Create(part, TweenInfo.new(0.1, Enum.EasingStyle.Linear), { Transparency = 1 })
        tween:Play()
        step(10)
        expect(fired > 0):ToBe(true)
    end)
end)

describe("TweenService:Create", function()
    local part: Part = nil :: any

    beforeEach(function()
        part = Instance.new("Part")
        part.Anchored = true
        part.Parent = workspace
    end)

    afterEach(function()
        part:Destroy()
    end)

    it("validates every goal at Create rather than at the first write", function()
        -- A tween that played for half a second and then reported a typo would
        -- be reporting it from the wrong place and at the wrong time.
        expect(function()
            return TweenService:Create(part, TweenInfo.new(), { NoSuchProperty = 1 })
        end):ToThrow("scene.err.unknown_property")

        expect(function()
            return TweenService:Create(part, TweenInfo.new(), { ClassName = "Wedge" })
        end):ToThrow("scene.err.read_only_property")

        expect(function()
            return TweenService:Create(part, TweenInfo.new(), { Transparency = "half" })
        end):ToThrow()
    end)

    it("refuses a property with no halfway", function()
        expect(function()
            return TweenService:Create(part, TweenInfo.new(), { Anchored = false })
        end):ToThrow("script.err.tween_goal_type")
        expect(function()
            return TweenService:Create(part, TweenInfo.new(), { Name = "Other" })
        end):ToThrow("script.err.tween_goal_type")
    end)

    it("tweens every interpolable type the surface has", function()
        local tween = TweenService:Create(part, TweenInfo.new(0.05, Enum.EasingStyle.Linear), {
            Transparency = 1,
            Size = vector.create(4, 4, 4),
            Color = Color3.new(1, 0, 0),
            CFrame = CFrame.new(10, 20, 30),
        })
        tween:Play()
        step(8)
        expect(part.Transparency):ToBe(1)
        expect(part.Size):ToBe(vector.create(4, 4, 4))
        expect(part.Color):ToBe(Color3.new(1, 0, 0))
        expect(part.CFrame.Position):ToBe(vector.create(10, 20, 30))
    end)
end)
