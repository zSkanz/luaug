--!strict
--- Conformance: the scheduling-adjacent global surface (api-design.md §1.1
--- "Removed globals", divergence #2 in §2.5, ADR 0015, rule R8).
---
--- "`wait`, `spawn`, `delay`, `tick`, `elapsedTime`, `loadstring`,
--- `getfenv`/`setfenv`, `newproxy`, `shared`; `_G` exists but is frozen-empty...
--- `io` does not exist in the game VM." There are no aliases and there is no
--- deprecation window, so absence is the contract and has to be asserted.
---
--- Reading a removed name is deliberately a *static* error too, so each absent
--- global is read through an explicit `:: any` cast. That makes every assertion
--- below trivially satisfiable by a broken harness, which is why the first case
--- is a positive control: if the globals the suite expects to be *present* are
--- missing, every absence case after it is meaningless and the control fails
--- first. See the docGaps note on how a spec is meant to observe a removed name.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("the global environment", function()
    -- Anti-vacuity control for every absence assertion in this file.
    it("binds the world globals and Luau builtins the removals are checked against", function()
        expect(game).Never:ToBeNil()
        expect(workspace).Never:ToBeNil()
        expect(task):ToBeA("table")
        expect(coroutine):ToBeA("table")
        expect(math):ToBeA("table")
        expect(table):ToBeA("table")
        expect(string):ToBeA("table")
        expect(utf8):ToBeA("table")
        expect(buffer):ToBeA("table")
        expect(vector):ToBeA("table")
        expect(os):ToBeA("table")
        expect(print):ToBeA("function")
        expect(warn):ToBeA("function")
        expect(assert):ToBeA("function")
        expect(pcall):ToBeA("function")
        expect(xpcall):ToBeA("function")
        expect(select):ToBeA("function")
        expect(typeof):ToBeA("function")
        expect(require):ToBeA("function")
    end)

    -- The removals themselves are NOT tested here, and cannot be: naming an
    -- undeclared global is a strict-mode error, so a spec cannot legally
    -- reference `wait` to prove it is absent, and a `:: any` cast around one
    -- tests the cast rather than the VM. Absence is a VM-level property and
    -- `engine/script/tests/sandbox_tests.cpp` asserts it against the sandboxed
    -- global table directly (M2 brief, ruling R-D).

    describe("_G", function()
        it("exists", function()
            expect(_G :: any):ToBeA("table")
        end)

        it("is frozen", function()
            expect(table.isfrozen(_G :: any)):ToBe(true)
        end)

        it("is empty", function()
            expect((next(_G :: any))):ToBeNil()
        end)

        it("has no member under any name", function()
            local globals = _G :: any
            expect(globals.shared):ToBeNil()
            expect(globals.game):ToBeNil()
            expect(globals.task):ToBeNil()
        end)

        it("raises when written to", function()
            expect(function()
                local globals = _G :: any
                globals.spec = true
            end):ToThrow()
        end)

        it("is still empty after a rejected write", function()
            pcall(function()
                local globals = _G :: any
                globals.spec = true
            end)
            expect((next(_G :: any))):ToBeNil()
        end)
    end)

    -- Divergence #2 leaves exactly one clock reachable from script code: `os.clock`.
    -- The rest of `os` is the process-facing surface a sandboxed game VM must not
    -- have (rule R4), and §1.1 limits the library to clock/time/date.
    describe("os", function()
        it("exposes clock", function()
            expect(os.clock):ToBeA("function")
            expect(os.clock()):ToBeA("number")
        end)

        it("exposes time", function()
            expect(os.time):ToBeA("function")
        end)

        it("exposes date", function()
            expect(os.date):ToBeA("function")
        end)

        it("does not expose the process-facing members", function()
            local limited = os :: any
            expect(limited.getenv):ToBeNil()
            expect(limited.exit):ToBeNil()
            expect(limited.execute):ToBeNil()
            expect(limited.remove):ToBeNil()
            expect(limited.rename):ToBeNil()
            expect(limited.tmpname):ToBeNil()
            expect(limited.setlocale):ToBeNil()
        end)
    end)

    describe("the task library", function()
        it("exposes the five v1 calls", function()
            expect(task.spawn):ToBeA("function")
            expect(task.defer):ToBeA("function")
            expect(task.delay):ToBeA("function")
            expect(task.wait):ToBeA("function")
            expect(task.cancel):ToBeA("function")
        end)

        it("names its calls in lowercase, with no PascalCase spelling", function()
            local library = task :: any
            expect(library.Spawn):ToBeNil()
            expect(library.Defer):ToBeNil()
            expect(library.Delay):ToBeNil()
            expect(library.Wait):ToBeNil()
            expect(library.Cancel):ToBeNil()
        end)

        it("does not carry the reserved parallel calls in v1", function()
            local library = task :: any
            expect(library.synchronize):ToBeNil()
            expect(library.desynchronize):ToBeNil()
        end)

        -- "No further renames... and no runtime aliases, ever" (§2.5): each call is
        -- its own function, not a second name for a neighbour.
        it("gives each of its calls a distinct implementation", function()
            local library = task :: any
            expect(library.delay).Never:ToBe(library.wait)
            expect(library.spawn).Never:ToBe(library.defer)
        end)

        -- §7 reads `@std/task` as identity with the global `task`, and the
        -- case that proves it arrives with `@std` itself: ADR 0030's
        -- convergence surface is explicitly not M2 scope (brief, NOT in scope),
        -- so there is no alias to require yet.
    end)
end)
