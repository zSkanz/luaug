--!strict
--- Conformance: the per-script sandbox (docs/api-design.md 1.1, 3; R4).
---
--- 3 promises each Script its own environment and 1.1 removes every handle a
--- script could have used to reach another one's: `_G` exists but is
--- frozen-empty, `shared` is gone, and there is no `getfenv`/`setfenv`. What a
--- strict spec can hold is the shape of that surface -- a frozen empty `_G`,
--- an `os` cut down to three clock functions, and a `script` global that is a
--- per-script value rather than a world-wide one.
---
--- The absence of a removed global is asserted in `removed_globals.spec.luau`,
--- which is separated out because naming a global that does not exist is
--- itself a strict-mode type error unless the generated definitions declare
--- those names as `nil`.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("_G", function()
    it("exists as a table", function()
        expect(_G):ToBeA("table")
    end)

    it("is empty", function()
        local globals = _G :: { [any]: any }
        -- Parenthesised: `next` returns a pair, and `expect` takes one value.
        expect((next(globals))):ToBeNil()
    end)

    it("is frozen", function()
        expect(table.isfrozen(_G :: { [any]: any })):ToBe(true)
    end)

    it("rejects a write, so it cannot become a channel between scripts", function()
        local globals = _G :: any
        expect(function()
            globals.smuggled = true
        end):ToThrow()
    end)

    it("stays empty after a rejected write", function()
        local globals = _G :: any
        pcall(function()
            globals.smuggled = true
        end)
        expect((next(_G :: { [any]: any }))):ToBeNil()
    end)
end)

describe("os", function()
    it("provides clock, time and date", function()
        expect(os.clock):ToBeA("function")
        expect(os.time):ToBeA("function")
        expect(os.date):ToBeA("function")
    end)

    it("returns a number from os.clock", function()
        expect(os.clock()):ToBeA("number")
    end)

    it("returns a number from os.time", function()
        expect(os.time()):ToBeA("number")
    end)

    -- 1.1 limits `os` to clock/time/date, and architecture 5 states the game
    -- VM has no environment-variable or process access at all.
    it("provides nothing that reaches the host process", function()
        local osLibrary = os :: any
        expect(osLibrary.getenv):ToBeNil()
        expect(osLibrary.execute):ToBeNil()
        expect(osLibrary.exit):ToBeNil()
        expect(osLibrary.remove):ToBeNil()
        expect(osLibrary.rename):ToBeNil()
        expect(osLibrary.tmpname):ToBeNil()
        expect(osLibrary.setlocale):ToBeNil()
    end)
end)

describe("per-script identity", function()
    -- If globals were shared between scripts, `script` could not be "the
    -- running Script instance" -- one table cannot hold two answers. The
    -- distinctness below is the strict-mode-observable half of the sandbox.
    it("binds script to a value that is not a world-wide singleton", function()
        expect(script).Never:ToBe(game)
        expect(script).Never:ToBe(workspace)
        expect(script).Never:ToBe(game:GetService("ScriptService"))
    end)

    it("keeps script inside the mount rather than at the root of the world", function()
        expect(script.Parent).Never:ToBeNil()
        expect(script:IsDescendantOf(game:GetService("ScriptService"))):ToBe(true)
    end)

    it("gives the world globals the same identity on every read", function()
        expect(game):ToBe(game)
        expect(workspace):ToBe(workspace)
        expect(script):ToBe(script)
    end)
end)
