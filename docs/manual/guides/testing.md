# Testing

There are two test conventions, and **they are not one suite**. The extension is
the difference, and it is deliberate: the two are globbed by different runners.

| | Your project | The engine's conformance suite |
|---|---|---|
| Glob | `tests/**/*.test.luau` | `tests/conformance/**/*.spec.luau` |
| Runner | Lute, with no engine | The headless engine |
| Library | `@std/test` | `@luaug/testing` |
| Sees | Pure Luau | The whole engine API |

A conformance spec matching the user pattern would be picked up by the pure
runner, where there is no engine to conform to and every case fails for the
wrong reason.

## Engine tests

```luau
--!strict
local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach = testing.beforeEach

local PhysicsService = game:GetService("PhysicsService")

--- One tick, expressed against FixedTimestep rather than as a literal, so the
--- suite stays correct at 30 Hz or 240 Hz.
local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("a falling part", function()
    local part: BasePart

    beforeEach(function()
        part = Instance.new("Part")
        part.Position = vector.create(0, 10, 0)
        part.Parent = workspace
    end)

    it("is moved by the simulation", function()
        local before = part.Position.y
        step(10)
        expect(part.Position.y < before):ToBe(true)
    end)
end)
```

Run it:

```bash
luaug test
luaug test tests/conformance/tween
luaug test --junit=results.xml
```

`luaug test` runs against the headless engine with the null renderer — a
conformance run is about the world, not about pixels, and asking for a device
would make the suite unrunnable on any machine without a GPU.

Output is TAP on standard output, with optional JUnit XML.

## Cases yield

A case body runs **sequentially and yield-transparently**: calling `task.wait`
inside one yields the runner with it. That is the only way a spec can observe a
deferred signal actually arriving, and it is why the helper above exists.

Nothing starts a coroutine of its own, so a case that yields forever hangs the
suite rather than passing quietly.

## Matchers

`testing.expect(value)` returns a matcher:

| Matcher | Checks |
|---|---|
| `ToBe` | Identity for values, `==` otherwise. |
| `ToEqual` | Structurally, with cycles compared by identity. |
| `ToBeNil` · `ToBeTruthy` · `ToBeFalsy` | |
| `ToBeCloseTo(expected, tolerance?)` | Absolute tolerance. |
| `ToBeA(typeName)` | Matches `typeof`, so engine datatypes work. |
| `ToContain(item)` | |
| `ToThrow(pattern?)` | The pattern is a **plain substring**. |

`.Never` negates any of them.

`ToThrow` taking a plain substring is what lets a spec match an engine error by
its key — `expect(fn):ToThrow("scene.err.parent_cycle")` — without escaping
anything.

The matchers are PascalCase because a matcher is an object;
`testing.describe` and friends are camelCase because a module is not.

## Project tests

For code that does not need the engine — a maths helper, a data transform — the
pure runner is faster and simpler:

```luau
--!strict
local assert = require("@std/test/assert")
local greeting = require("../src/shared/greeting")
local test = require("@std/test")

test.suite("greeting", function(suite)
    suite:case("is not empty", function()
        assert.that(#greeting.forProject() > 0, "a greeting is not empty")
    end)
end)
```

**These run under Lute, not under the engine.** The engine's own module surface
does not currently include `@std/test`, so a `*.test.luau` file is a pure test
and stays one.

## Checking before running

```bash
luaug check
```

Runs the analyzer with the engine's generated type definitions, and the
formatter in check mode. A wrong property name is an error there rather than a
surprise at runtime — which is what a fully typed API surface is for.

An empty check is a **failure**, not a pass: a run that collected zero files
exits non-zero. The same rule applies to a suite with zero cases, because an
empty suite passes forever.

## Where to look next

- [The luaug CLI](manual:get-started/cli)
- [Determinism](manual:concepts/determinism) — what a replay test is for
