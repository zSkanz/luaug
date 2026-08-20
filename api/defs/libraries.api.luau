--!strict
--- Global libraries and free globals (api-design.md §1.1, §3.2).
---
--- `task` is the whole scheduling surface and there are no legacy globals --
--- no `wait`, no `spawn`, no `delay`, no `tick` (divergence #2, R8). The
--- removals are enforced in the VM and tested from C++; what is declared here is
--- what remains.
---
--- Luau's own libraries -- `math`, `table`, `string`, `coroutine`, `buffer`,
--- `bit32`, `utf8`, `debug`, `os`, `vector` -- are NOT declared here. They are
--- the language's, the analyzer already knows them, and re-declaring them would
--- make this file a second source of truth for something the pin already fixes.

local Schema = require("../schema")

local Libraries: { Schema.Library } = {
    {
        Name = "task",
        Doc = "The scheduler, and the only way a script asks for time. Timers run on the SimClock rather than "
            .. "the wall clock, so `task.wait(1)` is exactly 60 ticks at the default timestep on every machine "
            .. "and every run -- which is what makes a recorded replay reproduce (ADR 0025) and why there is no "
            .. "`tick()`.",
        Functions = {
            {
                Name = "spawn",
                -- A generic pack, so `task.spawn(function(n: number) end, 1)`
                -- type-checks with the payload types intact. It has to be
                -- emitted UNNAMED -- `(fn: (T...) -> (), T...)` -- because a
                -- named type pack is a syntax error that takes the whole
                -- definitions file down with it, which is how this was found.
                Parameters = {
                    { Name = "fn", Type = "(T...) -> ()" },
                    { Name = "arguments", Type = "T..." },
                },
                Returns = { "thread" },
                ThreadSafety = "Unsafe",
                Doc = "Resumes `fn` on a new coroutine **immediately**, synchronously, before returning -- the "
                    .. "one non-deferred call in the library and deliberately so. Returns that thread. An error "
                    .. "inside it is contained and does not propagate to the caller (§3.1).",
            },
            {
                Name = "defer",
                Parameters = {
                    { Name = "fn", Type = "(T...) -> ()" },
                    { Name = "arguments", Type = "T..." },
                },
                Returns = { "thread" },
                ThreadSafety = "Unsafe",
                Doc = "Enqueues `fn` on the deferred queue, so it runs at the next resumption point ordered "
                    .. "against signal fires by when it was raised (§3.1). Returns the thread it will resume, "
                    .. "which is the handle `task.cancel` takes.",
            },
            {
                Name = "delay",
                Parameters = {
                    { Name = "duration", Type = "number" },
                    { Name = "fn", Type = "(T...) -> ()" },
                    { Name = "arguments", Type = "T..." },
                },
                Returns = { "thread" },
                ThreadSafety = "Unsafe",
                Doc = "Resumes `fn` at the first task-resume phase at or after `duration` seconds of SimClock "
                    .. "time. Zero and negative durations both mean the next tick: `delay` must not run before "
                    .. 'it returns, so "at or after 0 seconds" cannot be satisfied by the phase you are '
                    .. "standing in.",
            },
            {
                Name = "wait",
                Parameters = { { Name = "duration", Type = "number?", Default = "nil" } },
                Returns = { "number" },
                Yields = true,
                ThreadSafety = "Unsafe",
                Doc = "Yields, and resumes at the first task-resume phase at or after `duration` seconds of "
                    .. "SimClock time, defaulting to the next tick. Returns the elapsed sim time, which is a "
                    .. "whole number of ticks times the timestep and therefore at least `duration`. Parks "
                    .. "without an `Async` suffix because the name already says it waits (§9).",
            },
            {
                Name = "cancel",
                Parameters = { { Name = "thread", Type = "thread" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Cancels a pending resumption -- a queued `task.defer` entry counts as one, so cancelling "
                    .. "the thread `defer` returned stops its callback from running. A finished thread, an "
                    .. "already-cancelled one and the running one all raise `script.err.task_not_scheduled`: "
                    .. "none holds a pending resumption, and a pending resumption is the only thing this can "
                    .. "take away.",
            },
        } :: { Schema.Method },
    },
}

local Globals: { Schema.Method } = {
    {
        Name = "warn",
        Parameters = { { Name = "values", Type = "...any" } },
        Returns = {},
        ThreadSafety = "Unsafe",
        Doc = "Prints like `print` and differs from it in severity and in nothing else: tab-separated, "
            .. "`tostring` applied to each argument. It reaches `DebugService.MessageOut` at "
            .. "`Enum.LogLevel.Warning`, and its text passes through verbatim rather than through the message "
            .. "catalog -- R3 governs what the engine says, not what a game says.",
    },
}

local Definition: Schema.Definition = {
    Libraries = Libraries,
    Globals = Globals,
}

return Definition
