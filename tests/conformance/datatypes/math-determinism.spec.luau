--!strict
--- Conformance: a script's `math` and `^` are the same bits on every platform
--- (ADR 0083).
---
--- Every expected value below is what the engine's own transcendentals
--- (`core::dmath`) answer, and each input was chosen because Microsoft's C
--- runtime answers something one bit away. So this file fails on Windows if a
--- call reaches the runtime instead -- through a missed table entry, or through
--- a compiler fastcall that went past the table -- and on Linux and macOS the
--- same numbers prove the same thing against glibc and Apple's libm.
---
--- Compared with `ToBe`, never `ToBeCloseTo`: close is what every libm already
--- is, and identical is the whole claim.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("math is the same bits on every platform", function()
    it("answers sin, cos, exp and log with the engine's own", function()
        expect(math.sin(0.117)):ToBe(0.11673324714446585)
        expect(math.sin(0.403)):ToBe(0.39217976876466043)
        expect(math.cos(5.0429040149605324)):ToBe(0.32453022991253005)
        expect(math.exp(0.5064370209944522)):ToBe(1.6593683551229224)
        expect(math.log(1.5826929698958536)):ToBe(0.45912780750472176)
    end)

    it("answers atan2 and pow with the engine's own", function()
        expect(math.atan2(0.008, 1.3)):ToBe(0.0061537684739240324)
        expect(math.pow(0.004, 1.7)):ToBe(8.3849791157516659e-05)
    end)

    it("answers the same when the arguments arrive at run time", function()
        -- Through a table, so the compiler cannot see a constant and fold it,
        -- and the call is made with whatever the VM has in hand.
        local inputs = { 0.117, 0.004 }
        expect(math.sin(inputs[1])):ToBe(0.11673324714446585)
        expect(math.pow(inputs[2], 1.7)):ToBe(8.3849791157516659e-05)
    end)

    it("takes ^ through the same pow, folded or not", function()
        local base = { 0.004 }
        expect(base[1] ^ 1.7):ToBe(8.3849791157516659e-05)
        expect(0.004 ^ 1.7):ToBe(8.3849791157516659e-05)
    end)

    it("keeps what a script compares against a literal exact", function()
        expect(2 ^ 10):ToBe(1024)
        expect(10 ^ -2):ToBe(0.01)
        expect(math.pow(2, 0.5)):ToBe(math.sqrt(2))
        expect(math.log(8, 2)):ToBe(3)
        expect(math.log10(1000)):ToBe(3)
    end)
end)
