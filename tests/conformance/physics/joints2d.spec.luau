--!strict
--- Conformance: 2D joints and sprite animation (ADR 0102).
---
--- Against the real solver: a pendulum on a hinge swings about its point and
--- keeps its length, a rigid spring is a rod, a weld carries one part with
--- another, and a sprite sheet turns its pages on the simulation clock.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

local spawned: { Instance } = {}

local function part(position: Vector2, anchored: boolean): Part2D
    local created = Instance.new("Part2D")
    created.Position = position
    created.Size = Vector2.new(0.5, 0.5)
    created.Anchored = anchored
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function keep(instance: Instance)
    table.insert(spawned, instance)
end

local function cleanUp()
    for _, instance in spawned do
        instance:Destroy()
    end
    table.clear(spawned)
end

describe("HingeConstraint2D", function()
    afterEach(cleanUp)

    it("swings a bob about its pivot at the length it was hung", function()
        local pivot = part(Vector2.new(0, 10), true)
        local bob = part(Vector2.new(3, 10), false)
        local hinge = Instance.new("HingeConstraint2D")
        keep(hinge)
        hinge.Part0 = pivot
        hinge.Part1 = bob
        hinge.Anchor1 = Vector2.new(-3, 0)
        hinge.Parent = workspace
        step(30)
        -- It fell, but on its string.
        expect(bob.Position.Y < 9.5):ToBe(true)
        expect((bob.Position - pivot.Position).Magnitude):ToBeCloseTo(3, 0.05)
    end)

    it("holds nothing while disabled", function()
        local pivot = part(Vector2.new(0, 10), true)
        local bob = part(Vector2.new(3, 10), false)
        local hinge = Instance.new("HingeConstraint2D")
        keep(hinge)
        hinge.Part0 = pivot
        hinge.Part1 = bob
        hinge.Anchor1 = Vector2.new(-3, 0)
        hinge.Enabled = false
        hinge.Parent = workspace
        step(30)
        -- Straight down, unheld.
        expect(bob.Position.X):ToBeCloseTo(3, 0.01)
    end)
end)

describe("SpringConstraint2D", function()
    afterEach(cleanUp)

    it("is a rod at a stiffness of zero", function()
        local top = part(Vector2.new(0, 10), true)
        local weight = part(Vector2.new(0, 8), false)
        local rod = Instance.new("SpringConstraint2D")
        keep(rod)
        rod.Part0 = top
        rod.Part1 = weight
        rod.Length = 2
        rod.Stiffness = 0
        rod.Parent = workspace
        step(60)
        expect(weight.Position.Y):ToBeCloseTo(8, 0.02)
    end)
end)

describe("WeldConstraint2D", function()
    afterEach(cleanUp)

    it("carries one part with the other as they stood", function()
        local body = part(Vector2.new(0, 10), false)
        local lid = part(Vector2.new(0, 10.5), false)
        local weld = Instance.new("WeldConstraint2D")
        keep(weld)
        weld.Part0 = body
        weld.Part1 = lid
        weld.Anchor0 = Vector2.new(0, 0.5)
        weld.Parent = workspace
        step(30)
        expect(body.Position.Y < 9.5):ToBe(true)
        expect(lid.Position.Y - body.Position.Y):ToBeCloseTo(0.5, 0.02)
    end)
end)

describe("SpriteAnimator", function()
    afterEach(cleanUp)

    it("turns a sheet's pages and stops at the end of a run that is not looped", function()
        local sprite = part(Vector2.new(0, 0), true)
        local clip = Instance.new("SpriteAnimator")
        clip.FrameSize = Vector2.new(8, 8)
        clip.Columns = 4
        clip.FrameCount = 3
        -- A page a tick.
        clip.FramesPerSecond = 1 / PhysicsService.FixedTimestep
        clip.Looped = false
        clip.Parent = sprite
        local ended = false
        clip:GetPropertyChangedSignal("Playing"):Connect(function()
            ended = not clip.Playing
        end)
        clip.Playing = true
        step(1)
        expect(sprite.ImageRectSize.X):ToBe(8)
        step(10)
        expect(clip.Playing):ToBe(false)
        expect(clip.Frame):ToBe(2)
        expect(sprite.ImageRectOffset.X):ToBe(16)
        expect(ended):ToBe(true)
    end)
end)
