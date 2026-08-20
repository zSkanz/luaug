--!strict
--- `@luaug/testing` -- the conformance test library (architecture.md §9).
---
--- Deliberately free of any engine dependency: it registers a tree of suites
--- and cases and then runs them. That is what lets the same spec files run
--- under the headless engine (`luaug-host --run-tests`, the gate) and under
--- Lute for fast iteration, and it is why this file type-checks before a single
--- Instance binding exists.
---
--- Cases run **sequentially and yield-transparently**: a case body that calls
--- `task.wait` yields the runner along with it, which is the only way a spec
--- can observe a deferred signal actually arriving at the next resumption
--- point. Nothing here starts a coroutine of its own.
---
--- Casing follows ADR 0034: this is a module, so its exports are camelCase,
--- while `Matcher` is an object and its methods are PascalCase.

local Testing = {}

export type Matcher = {
    --- Identity for values, `==` for everything else.
    ToBe: (self: Matcher, expected: any) -> (),
    --- Structural equality, recursing into tables (cycles are compared by identity).
    ToEqual: (self: Matcher, expected: any) -> (),
    ToBeNil: (self: Matcher) -> (),
    ToBeTruthy: (self: Matcher) -> (),
    ToBeFalsy: (self: Matcher) -> (),
    --- Absolute tolerance, because the values under test are engine floats.
    ToBeCloseTo: (self: Matcher, expected: number, tolerance: number?) -> (),
    --- `expect(v):ToBeA("string")`, matching `typeof` so engine datatypes work.
    ToBeA: (self: Matcher, typeName: string) -> (),
    --- Array membership, by `==`.
    ToContain: (self: Matcher, item: any) -> (),
    --- The value must be a function; the optional pattern is matched against the
    --- error with `string.find(..., pattern, 1, true)` -- a plain substring, so
    --- an i18n key like `[scene.err.parent_cycle]` needs no escaping.
    ToThrow: (self: Matcher, pattern: string?) -> (),
    --- Negates every matcher above: `expect(x).Never:ToBe(y)`.
    Never: Matcher,
}

export type Failure = {
    --- Suite names outermost-first, with the case name last.
    Path: { string },
    Message: string,
    Traceback: string?,
}

--- One case's outcome, in the order the cases ran. `Failures` carries the same
--- information for the ones that failed; this carries every case, because a
--- runner that reports only failures cannot say what it *did* -- and TAP, which
--- `luaug test` emits, is a line per case rather than a line per problem.
export type CaseResult = {
    Path: { string },
    Ok: boolean,
    Message: string?,
}

export type Report = {
    Total: number,
    Passed: number,
    Failed: number,
    Failures: { Failure },
    Cases: { CaseResult },
}

type Case = {
    Name: string,
    Body: () -> (),
}

type Suite = {
    Name: string,
    Cases: { Case },
    Children: { Suite },
    Parent: Suite?,
    BeforeEach: { () -> () },
    AfterEach: { () -> () },
}

local function newSuite(name: string, parent: Suite?): Suite
    return {
        Name = name,
        Cases = {},
        Children = {},
        Parent = parent,
        BeforeEach = {},
        AfterEach = {},
    }
end

local Root: Suite = newSuite("")
local Current: Suite = Root

local function describeValue(value: any): string
    local kind = typeof(value)
    if kind == "string" then
        return string.format("%q", value :: string)
    elseif kind == "table" then
        -- Tables are compared structurally, so their address says nothing useful
        -- about why a comparison failed; the caller's own message has to.
        return "<table>"
    end
    return tostring(value)
end

local function deepEqual(a: any, b: any, seen: { [any]: any }): boolean
    if a == b then
        return true
    end
    if typeof(a) ~= "table" or typeof(b) ~= "table" then
        return false
    end
    -- Cycles fall back to identity, which `a == b` above already ruled out.
    if seen[a] == b then
        return true
    end
    seen[a] = b

    local left = a :: { [any]: any }
    local right = b :: { [any]: any }
    for key, value in left do
        if not deepEqual(value, right[key], seen) then
            return false
        end
    end
    for key in right do
        if left[key] == nil then
            return false
        end
    end
    return true
end

--- Raised by a failing matcher. `run` catches these and records them; anything
--- else that escapes a case body is recorded as an error with its traceback.
local FailurePrefix = "luaug.testing.failure: "

local function fail(message: string): ()
    error(FailurePrefix .. message, 3)
end

--- `pcall` is typed to return exactly what its callback returns, so calling it
--- on a `() -> ()` yields one value and `local ok, err = pcall(body)` is a
--- strict-mode error -- and `pcall` itself cannot be cast to a narrower shape,
--- being generic over both A... and R.... The wrapper gives the call a return
--- type, once, instead of five casts at the call sites.
local function protectedCall(fn: () -> ()): (boolean, any)
    return pcall(function(): any
        fn()
        return nil
    end)
end

local function newMatcher(value: any, negated: boolean): Matcher
    local function check(ok: boolean, describeExpectation: string): ()
        if ok == negated then
            local prefix = if negated then "expected not " else "expected "
            fail(prefix .. describeExpectation .. ", got " .. describeValue(value))
        end
    end

    local matcher = {} :: any

    function matcher.ToBe(_self: Matcher, expected: any): ()
        check(value == expected, describeValue(expected))
    end

    function matcher.ToEqual(_self: Matcher, expected: any): ()
        check(deepEqual(value, expected, {}), "to deep-equal " .. describeValue(expected))
    end

    function matcher.ToBeNil(_self: Matcher): ()
        check(value == nil, "nil")
    end

    function matcher.ToBeTruthy(_self: Matcher): ()
        check(not not value, "a truthy value")
    end

    function matcher.ToBeFalsy(_self: Matcher): ()
        check(not value, "a falsy value")
    end

    function matcher.ToBeCloseTo(_self: Matcher, expected: number, tolerance: number?): ()
        local epsilon = tolerance or 1e-6
        local ok = typeof(value) == "number" and math.abs((value :: number) - expected) <= epsilon
        check(ok, string.format("within %g of %g", epsilon, expected))
    end

    function matcher.ToBeA(_self: Matcher, typeName: string): ()
        check(typeof(value) == typeName, "a " .. typeName .. ", not a " .. typeof(value))
    end

    function matcher.ToContain(_self: Matcher, item: any): ()
        local found = false
        if typeof(value) == "table" then
            for _, entry in value :: { any } do
                if entry == item then
                    found = true
                    break
                end
            end
        end
        check(found, "to contain " .. describeValue(item))
    end

    function matcher.ToThrow(_self: Matcher, pattern: string?): ()
        if typeof(value) ~= "function" then
            fail("ToThrow expects a function, got " .. typeof(value))
        end
        local ok, err = protectedCall(value :: () -> ())
        if ok then
            check(false, "the call to throw")
            return
        end
        local text = tostring(err)
        if pattern == nil then
            check(true, "the call to throw")
            return
        end
        check(
            string.find(text, pattern, 1, true) ~= nil,
            "the error to contain " .. describeValue(pattern) .. ", got " .. describeValue(text)
        )
    end

    if not negated then
        matcher.Never = newMatcher(value, true)
    end

    return matcher :: Matcher
end

function Testing.expect(value: any): Matcher
    return newMatcher(value, false)
end

--- Groups cases. Nesting is allowed; `beforeEach`/`afterEach` apply to every
--- case in the suite and its descendants, outermost first for `beforeEach` and
--- innermost first for `afterEach`.
function Testing.describe(name: string, body: () -> ()): ()
    local suite = newSuite(name, Current)
    table.insert(Current.Children, suite)

    local previous = Current
    Current = suite
    -- Registration must not be able to swallow a case: if the body errors here
    -- the suite is simply empty, and an empty suite is a visible zero rather
    -- than a silently missing file.
    local ok, err = protectedCall(body)
    Current = previous
    if not ok then
        error(err, 0)
    end
end

function Testing.it(name: string, body: () -> ()): ()
    table.insert(Current.Cases, { Name = name, Body = body })
end

function Testing.beforeEach(fn: () -> ()): ()
    table.insert(Current.BeforeEach, fn)
end

function Testing.afterEach(fn: () -> ()): ()
    table.insert(Current.AfterEach, fn)
end

local function hooksFor(suite: Suite): ({ () -> () }, { () -> () })
    local befores: { () -> () } = {}
    local afters: { () -> () } = {}

    local chain: { Suite } = {}
    local node: Suite? = suite
    while node do
        table.insert(chain, 1, node)
        node = node.Parent
    end

    for _, entry in chain do
        for _, fn in entry.BeforeEach do
            table.insert(befores, fn)
        end
    end
    for index = #chain, 1, -1 do
        for _, fn in chain[index].AfterEach do
            table.insert(afters, fn)
        end
    end
    return befores, afters
end

local function cleanMessage(err: any): string
    local text = tostring(err)
    local stripped = string.gsub(text, "^" .. FailurePrefix, "", 1)
    return stripped
end

local function runSuite(suite: Suite, path: { string }, report: Report): ()
    for _, case in suite.Cases do
        local casePath = table.clone(path)
        table.insert(casePath, case.Name)
        report.Total += 1

        local befores, afters = hooksFor(suite)
        local failure: Failure? = nil

        local function record(err: any): ()
            if failure == nil then
                failure = {
                    Path = casePath,
                    Message = cleanMessage(err),
                    Traceback = debug.traceback(nil, 2),
                }
            end
        end

        for _, hook in befores do
            local ok, err = protectedCall(hook)
            if not ok then
                record(err)
                break
            end
        end

        if failure == nil then
            local ok, err = protectedCall(case.Body)
            if not ok then
                record(err)
            end
        end

        -- afterEach runs even when the case failed: a spec that creates
        -- instances must be able to destroy them, or one failure cascades into
        -- every case that follows it.
        for _, hook in afters do
            local ok, err = protectedCall(hook)
            if not ok then
                record(err)
            end
        end

        if failure then
            report.Failed += 1
            table.insert(report.Failures, failure)
            table.insert(report.Cases, { Path = casePath, Ok = false, Message = (failure :: Failure).Message })
        else
            report.Passed += 1
            table.insert(report.Cases, { Path = casePath, Ok = true })
        end
    end

    for _, child in suite.Children do
        local childPath = table.clone(path)
        table.insert(childPath, child.Name)
        runSuite(child, childPath, report)
    end
end

--- Runs everything registered so far and clears the registry, so a runner that
--- loads spec files one at a time gets one report per file.
function Testing.run(): Report
    local report: Report = { Total = 0, Passed = 0, Failed = 0, Failures = {}, Cases = {} }
    runSuite(Root, {}, report)
    Root = newSuite("")
    Current = Root
    return report
end

--- Formats a report as human-readable lines. The engine runner prints these;
--- the machine-readable TAP/JUnit output is `luaug test` in M3.
function Testing.format(report: Report): string
    local lines: { string } = {}
    for _, failure in report.Failures do
        table.insert(lines, "FAIL " .. table.concat(failure.Path, " > "))
        table.insert(lines, "     " .. failure.Message)
    end
    table.insert(lines, string.format("%d passed, %d failed, %d total", report.Passed, report.Failed, report.Total))
    return table.concat(lines, "\n")
end

return Testing
