--!strict
--- Conformance: `Script.Enabled` after boot (ADR 0059).
---
--- `Enabled` gated `startScripts` and did nothing afterwards, and the IDL said
--- so. It does something now, and the contract is deliberately narrow: **it
--- decides whether this script's threads are resumed, and nothing else.**
---
--- What is asserted here is that narrowness as much as the feature. A disable
--- that also disconnected, or destroyed, or undid what a script built would
--- answer a question this repository has twice declined to answer on purpose --
--- whether a connection dies with the thing its closure captured (ADR 0058) --
--- and a property setter is the worst place to answer it.
---
--- **Two things are deliberately not covered and neither is an oversight.** A
--- thread already executing runs to its next yield, because a coroutine cannot
--- be preempted; there is no way to observe "still running" from another
--- coroutine that is not itself a race. And `task.cancel` on a thread belonging
--- to a disabled script is the reason rule 4 drops lazily rather than sweeping:
--- the thread it would cancel belongs to the disabled script, so only that
--- script could call it, and it is disabled.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local sandbox: Folder? = nil

local function folder(): Folder
    return assert(sandbox, "the sandbox folder is created in beforeEach")
end

--- A `Script` that is in the tree and has never started.
---
--- Created disabled, because creating one enabled would not start it either --
--- scripts start when the world does, and this is long after -- and a spec that
--- relied on that would be asserting the absence of a feature rather than the
--- presence of this one.
local function sleeping(name: string, source: string): Script
    local script = Instance.new("Script")
    script.Name = name
    script.Enabled = false
    script.Source = source
    script.Parent = folder()
    return script
end

--- The names under the sandbox, in the order they were parented, minus the
--- scripts themselves.
---
--- Returned JOINED rather than as a list, and that is not cosmetic: a failed
--- table comparison reports `expected to deep-equal <table>, got <table>`, which
--- says nothing about which world you actually have. Order is the point in one
--- case below, so the separator carries it.
local function made(): string
    local names = {}
    for _, child in folder():GetChildren() do
        if child.ClassName ~= "Script" then
            table.insert(names, child.Name)
        end
    end
    return table.concat(names, ",")
end

--- Source that parents one part named `name` into the sandbox, found by name
--- because a script has no reference to anything the spec holds.
local function makes(name: string): string
    return `local p = Instance.new("Part") p.Name = "{name}" `
        .. `p.Parent = workspace:FindFirstChild("ScriptEnabledSpec")`
end

describe("Script.Enabled", function()
    beforeEach(function()
        local created = Instance.new("Folder")
        created.Name = "ScriptEnabledSpec"
        created.Parent = workspace
        sandbox = created
    end)

    afterEach(function()
        folder():Destroy()
        sandbox = nil
    end)

    describe("false to true", function()
        it("starts the script, once, at the next drain", function()
            local waker = sleeping("Waker", makes("MadeOnEnable"))
            expect(made()):ToBe("")

            waker.Enabled = true
            task.wait()

            expect(made()):ToBe("MadeOnEnable")

            -- Once. A start that re-ran on every drain would show up here and
            -- nowhere else, because the world it builds looks correct after the
            -- first one.
            task.wait()
            task.wait()
            expect(made()):ToBe("MadeOnEnable")
        end)

        it("writing true over true does not start it a second time", function()
            local script = sleeping("Idempotent", makes("Once"))
            script.Enabled = true
            task.wait()
            expect(made()):ToBe("Once")

            script.Enabled = true
            task.wait()
            expect(made()):ToBe("Once")
        end)

        it("runs the file scope again on a re-enable, because it is a start", function()
            -- **A re-enable is a start and not a resume.** There is no old thread
            -- to hand back, and pretending otherwise would make `Enabled` two
            -- different operations depending on a history nobody can see.
            local script = sleeping("Twice", makes("Ran"))
            script.Enabled = true
            task.wait()
            expect(made()):ToBe("Ran")

            script.Enabled = false
            task.wait()
            script.Enabled = true
            task.wait()

            -- Two parts of the same name: the second run built its own.
            expect(made()):ToBe("Ran,Ran")
        end)

        it("starts several enabled in one tick in document order", function()
            -- The R10 claim. Written in the reverse order they are enabled, so a
            -- pass cannot come from the writes happening to be in tree order --
            -- and a pool artefact deciding this would show up as `Second, First`.
            local first = sleeping("First", makes("FromFirst"))
            local second = sleeping("Second", makes("FromSecond"))

            second.Enabled = true
            first.Enabled = true
            task.wait()

            expect(made()):ToBe("FromFirst,FromSecond")
        end)
    end)

    describe("true to false", function()
        it("stops a connection being invoked without disconnecting it", function()
            -- **Its own target, destroyed by this case rather than by `afterEach`.**
            -- A connection outlives the script that made it -- that is this rule --
            -- and it also outlives the script being DESTROYED, which nothing here
            -- decides and nothing here should. So the handler is given somewhere to
            -- write that stops existing when this case ends: after that its lookup
            -- answers nil, the part it makes is parented to nothing, and no later
            -- case inherits a Ticker writing into its sandbox.
            local ticks = Instance.new("Folder")
            ticks.Name = "ScriptEnabledTicks"
            ticks.Parent = workspace

            local function ticked(): number
                return #ticks:GetChildren()
            end

            local script = sleeping(
                "Ticker",
                [[
					local RunService = game:GetService("RunService")
					RunService.Heartbeat:Connect(function()
						local p = Instance.new("Part")
						p.Name = "Tick"
						p.Parent = workspace:FindFirstChild("ScriptEnabledTicks")
					end)
				]]
            )

            script.Enabled = true
            task.wait()
            task.wait()
            local whileEnabled = ticked()
            expect(whileEnabled > 0):ToBe(true)

            script.Enabled = false
            task.wait()
            task.wait()
            task.wait()
            expect(ticked()):ToBe(whileEnabled)

            -- **And it is still connected**, which is the half that makes this one
            -- sentence rather than a lifetime rule: re-enabling starts a second file
            -- scope, and the count moves again.
            script.Enabled = true
            task.wait()
            task.wait()
            expect(ticked() > whileEnabled):ToBe(true)

            script.Enabled = false
            task.wait()
            ticks:Destroy()
        end)

        it("drops a task.delay that comes due after the disable", function()
            local script = sleeping(
                "Late",
                [[
					task.delay(0.1, function()
						local p = Instance.new("Part")
						p.Name = "TooLate"
						p.Parent = workspace:FindFirstChild("ScriptEnabledSpec")
					end)
				]]
            )

            script.Enabled = true
            task.wait()
            script.Enabled = false

            -- Well past the deadline at any tick rate this engine runs.
            for _ = 1, 30 do
                task.wait()
            end
            expect(made()):ToBe("")
        end)

        it("leaves what the script built where it is", function()
            -- Somebody will expect a disable to clean up. It does not, and the
            -- alternative is this rule growing into ownership semantics for
            -- every instance a script ever touched.
            local script = sleeping("Builder", makes("Survivor"))
            script.Enabled = true
            task.wait()
            expect(made()):ToBe("Survivor")

            script.Enabled = false
            task.wait()
            expect(made()):ToBe("Survivor")
        end)
    end)
end)
