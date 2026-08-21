--!strict
--- `Vector2`, `UDim`, `UDim2` and `Rect`, from api-design.md §2.3.
---
--- The four screen-space value types, written against the specification rather
--- than against the binding. They are one file because they are one vocabulary:
--- a `UDim2` is two `UDim`s, a `Rect` is two `Vector2`s, and the interesting
--- cases are the ones that cross between them.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("Vector2", function()
    it("constructs from two components, each defaulting to zero", function()
        local v = Vector2.new(3, 4)
        expect(v.X):ToBe(3)
        expect(v.Y):ToBe(4)
        expect(Vector2.new().X):ToBe(0)
        expect(Vector2.new().Y):ToBe(0)
        expect(Vector2.new(5).Y):ToBe(0)
    end)

    it("is typeof Vector2", function()
        expect(typeof(Vector2.new())):ToBe("Vector2")
    end)

    it("carries zero and one as constants", function()
        expect(Vector2.zero):ToBe(Vector2.new(0, 0))
        expect(Vector2.one):ToBe(Vector2.new(1, 1))
    end)

    it("reports its magnitude and its unit", function()
        local v = Vector2.new(3, 4)
        expect(v.Magnitude):ToBe(5)
        expect(v.Unit.X):ToBeCloseTo(0.6, 1e-6)
        expect(v.Unit.Y):ToBeCloseTo(0.8, 1e-6)
    end)

    it("normalizes the zero vector to zero rather than to NaN", function()
        -- The behaviour Vector3 already has. A stationary input axis produces
        -- this every tick it is untouched, so a NaN here would spread.
        local unit = Vector2.zero.Unit
        expect(unit.X):ToBe(0)
        expect(unit.Y):ToBe(0)
    end)

    it("adds, subtracts and negates component-wise", function()
        expect(Vector2.new(1, 2) + Vector2.new(10, 20)):ToBe(Vector2.new(11, 22))
        expect(Vector2.new(10, 20) - Vector2.new(1, 2)):ToBe(Vector2.new(9, 18))
        expect(-Vector2.new(1, -2)):ToBe(Vector2.new(-1, 2))
    end)

    it("scales with the number on either side", function()
        expect(Vector2.new(1, 2) * 3):ToBe(Vector2.new(3, 6))
        expect(3 * Vector2.new(1, 2)):ToBe(Vector2.new(3, 6))
        expect(Vector2.new(6, 8) / 2):ToBe(Vector2.new(3, 4))
    end)

    it("multiplies and divides component-wise against another Vector2", function()
        expect(Vector2.new(2, 3) * Vector2.new(10, 100)):ToBe(Vector2.new(20, 300))
        expect(Vector2.new(20, 300) / Vector2.new(10, 100)):ToBe(Vector2.new(2, 3))
    end)

    it("dots and lerps", function()
        expect(Vector2.new(1, 2):Dot(Vector2.new(3, 4))):ToBe(11)
        expect(Vector2.new(0, 0):Lerp(Vector2.new(10, 20), 0.25)):ToBe(Vector2.new(2.5, 5))
    end)

    it("extrapolates past the ends of a lerp rather than clamping", function()
        expect(Vector2.new(0, 0):Lerp(Vector2.new(10, 0), 2)):ToBe(Vector2.new(20, 0))
    end)

    it("compares exactly and component-wise", function()
        expect(Vector2.new(1, 2)):ToBe(Vector2.new(1, 2))
        expect(Vector2.new(1, 2)).Never:ToBe(Vector2.new(1, 2.0001))
    end)

    it("is immutable", function()
        expect(function()
            (Vector2.new(1, 2) :: any).X = 5
        end):ToThrow()
    end)
end)

describe("UDim", function()
    it("constructs from a scale and an offset, both defaulting to zero", function()
        local u = UDim.new(0.5, -8)
        expect(u.Scale):ToBe(0.5)
        expect(u.Offset):ToBe(-8)
        expect(UDim.new().Scale):ToBe(0)
        expect(UDim.new().Offset):ToBe(0)
    end)

    it("is typeof UDim", function()
        expect(typeof(UDim.new())):ToBe("UDim")
    end)

    it("adds and subtracts both halves", function()
        expect(UDim.new(0.5, 10) + UDim.new(0.25, 5)):ToBe(UDim.new(0.75, 15))
        expect(UDim.new(0.5, 10) - UDim.new(0.25, 5)):ToBe(UDim.new(0.25, 5))
    end)

    it("does not clamp a scale to 0-1", function()
        -- A scale past the parent is legal and means what it says; clamping here
        -- would silently rewrite a deliberate overhang.
        expect(UDim.new(1.5, 0).Scale):ToBe(1.5)
        expect(UDim.new(-0.5, 0).Scale):ToBe(-0.5)
    end)
end)

describe("UDim2", function()
    it("constructs from four numbers in axis order", function()
        local u = UDim2.new(0.5, -8, 1, 12)
        expect(u.X):ToBe(UDim.new(0.5, -8))
        expect(u.Y):ToBe(UDim.new(1, 12))
    end)

    it("is typeof UDim2", function()
        expect(typeof(UDim2.new())):ToBe("UDim2")
    end)

    it("constructs from scale alone and from offset alone", function()
        expect(UDim2.fromScale(0.5, 1)):ToBe(UDim2.new(0.5, 0, 1, 0))
        expect(UDim2.fromOffset(40, 60)):ToBe(UDim2.new(0, 40, 0, 60))
    end)

    it("adds and subtracts both axes", function()
        expect(UDim2.new(0.5, 10, 0.25, 5) + UDim2.new(0.5, 10, 0.25, 5)):ToBe(UDim2.new(1, 20, 0.5, 10))
        expect(UDim2.new(1, 20, 0.5, 10) - UDim2.new(0.5, 10, 0.25, 5)):ToBe(UDim2.new(0.5, 10, 0.25, 5))
    end)

    it("lerps all four numbers independently", function()
        -- The operation a tween on a Position performs, on the type, so that
        -- animating by hand and animating by tween agree at every step.
        local from = UDim2.new(0, 0, 0, 0)
        local to = UDim2.new(1, 100, 0.5, 40)
        expect(from:Lerp(to, 0.5)):ToBe(UDim2.new(0.5, 50, 0.25, 20))
    end)
end)

describe("Rect", function()
    it("constructs from two corners", function()
        local r = Rect.new(Vector2.new(1, 2), Vector2.new(11, 22))
        expect(r.Min):ToBe(Vector2.new(1, 2))
        expect(r.Max):ToBe(Vector2.new(11, 22))
    end)

    it("is typeof Rect", function()
        expect(typeof(Rect.new(Vector2.zero, Vector2.one))):ToBe("Rect")
    end)

    it("reports width and height as max minus min", function()
        local r = Rect.new(Vector2.new(1, 2), Vector2.new(11, 22))
        expect(r.Width):ToBe(10)
        expect(r.Height):ToBe(20)
    end)

    it("does not sort its corners", function()
        -- An inverted rectangle reports a negative size rather than zero. It is
        -- a mistake for its author to see: a SliceCenter silently corrected is
        -- a nine-slice that draws wrong with no way to find out why.
        local inverted = Rect.new(Vector2.new(10, 10), Vector2.new(0, 0))
        expect(inverted.Width):ToBe(-10)
        expect(inverted.Height):ToBe(-10)
        expect(inverted.Min):ToBe(Vector2.new(10, 10))
    end)

    it("refuses a corner that is not a Vector2", function()
        expect(function()
            return (Rect.new :: any)(1, 2)
        end):ToThrow()
    end)
end)

describe("The screen-space types as attributes", function()
    it("round-trip through SetAttribute", function()
        -- api-design.md §2.2 admits them "as those datatypes ship", and they
        -- ship here. Nothing about one is a tree edge, which is the only reason
        -- an Instance is excluded from the domain.
        local folder = Instance.new("Folder")
        folder:SetAttribute("Anchor", Vector2.new(0.5, 1))
        folder:SetAttribute("Inset", UDim.new(0, -8))
        folder:SetAttribute("Where", UDim2.fromScale(0.25, 0.75))
        folder:SetAttribute("Slice", Rect.new(Vector2.new(4, 4), Vector2.new(12, 12)))

        expect(folder:GetAttribute("Anchor")):ToBe(Vector2.new(0.5, 1))
        expect(folder:GetAttribute("Inset")):ToBe(UDim.new(0, -8))
        expect(folder:GetAttribute("Where")):ToBe(UDim2.fromScale(0.25, 0.75))
        expect(folder:GetAttribute("Slice")):ToBe(Rect.new(Vector2.new(4, 4), Vector2.new(12, 12)))
    end)
end)
