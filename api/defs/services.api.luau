--!strict
--- Services and the DataModel (api-design.md §2.1, §1.2).
---
--- The M2 subset. §2.1 lists the complete v1 set; the ones missing here are
--- missing because the systems behind them are later milestones.
---
--- One thing the schema cannot carry: `GetService` is declared with its general
--- `(name: string) -> Instance` overload alone. §1.2's string-singleton
--- overloads are the typed half of the same call and `Schema.Method` has no
--- overload list, so the general form is what is written down and the typed
--- forms are owed to the `.d.luau` generator; see the method's own note.
---
--- `RunService.Heartbeat` and `DebugService.MessageOut` are ordinary `Events`.
--- Neither name is a past-tense fact or a `Pre*`/`Post*` phase and both are
--- frozen public surface, which is what the schema's `EventNameExceptions` set
--- is for -- the rule carries the exception rather than the names carrying a
--- suffix.
---
--- Thread safety is annotated conservatively: `Safe` only where the value is
--- immutable, `ReadParallel` where the engine settles it before a tick and code
--- merely reads it, and `Unsafe` everywhere else. v1 runs serially (§3), so
--- nothing observes these yet -- and widening `Unsafe` to `ReadParallel` later
--- is a compatible change while the reverse is not.

local Schema = require("../schema")

-- The annotations below -- this local, and the `:: { Schema.X }` on every
-- nested list -- are load-bearing, not decoration. Without one at a given
-- level, the new solver infers that array's element type from the entries
-- themselves and then demands every entry carry the same optional fields, so
-- one method with a `Default` makes every other method missing it an error.
-- Each is an assertion rather than a cast: a wrong field name or a `Tags` entry
-- outside `ClassTag` still fails analysis.
local Classes: { Schema.Class } = {
    {
        Name = "DataModel",
        Extends = "Instance",
        Module = "scene",
        -- Not a `Service` itself: it is the parent every service is a child of,
        -- and it is reached through the `game` global rather than through
        -- `GetService`.
        Tags = { "NotCreatable" } :: { Schema.ClassTag },
        Properties = {
            {
                Name = "EngineVersion",
                Type = "string",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The version of the engine runtime executing this VM, as `luaug.toml` pins it. Read-only: "
                    .. "the runtime is chosen before the VM exists, so nothing a script does can change it.",
            },
            {
                Name = "LuauVersion",
                Type = "string",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The version of the Luau language runtime this engine build embeds. Read-only, and fixed per "
                    .. "engine release: a language upgrade is an engine release rather than a silent change "
                    .. "underneath a running project.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "GetService",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Instance" },
                ThreadSafety = "Unsafe",
                Doc = "Returns the service of this name, creating it on the first request. Services are singletons: "
                    .. "every later call returns the same instance, and it is an ordinary child of `game` once "
                    .. "created. A string literal gives an exactly typed result; a name computed at runtime gives "
                    .. "the general `Instance`, which is the honest cost of not knowing the name at analysis time. "
                    .. "An unknown name raises `scene.err.unknown_service`.",
            },
            {
                Name = "FindService",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Instance?" },
                ThreadSafety = "ReadParallel",
                Doc = "Returns the service of this name if it already exists, and `nil` otherwise. It creates "
                    .. "nothing, so it answers whether a service is in use rather than forcing it into existence "
                    .. "-- which is the whole difference from `GetService`.",
            },
            {
                Name = "BindToClose",
                Parameters = { { Name = "callback", Type = "() -> ()" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Registers a function to run while the game is shutting down, for work that has to happen "
                    .. "before the process ends. The callbacks run against a capped timeout and the shutdown "
                    .. "proceeds when it expires, finished or not: a close handler is a chance to finish, never a "
                    .. "veto.",
            },
            {
                Name = "Shutdown",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Ends the run. The registered `BindToClose` callbacks are given their timeout and the process "
                    .. "then exits. This is how a game closes itself deliberately.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Loaded",
                Parameters = {},
                Doc = "Fires once, after every entry script has had its first resumption. Connecting at file scope "
                    .. "inside an entry script still works: the fire is deferred like every other, so a handler "
                    .. "registered while the world is still being built is registered in time. It carries no "
                    .. "arguments.",
            },
        } :: { Schema.Event },
        Doc = "The root of the instance tree, reached through the `game` global. Services are its children: "
            .. "`Workspace` and `ScriptService` exist from boot and every other service is created by its first "
            .. "`GetService`. It is the services' parent rather than one of them.",
    },
    {
        Name = "Workspace",
        Extends = "Instance",
        Module = "scene",
        Storage = "Workspace",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        -- `CurrentCamera` arrived with the renderer at M4 and `Gravity` with
        -- the simulation at M5, as the note here always said they would.
        Doc = "The root of the 3D scene, reached through the `workspace` global as well as through "
            .. "`game:GetService`. Whatever is parented under it is in the world and whatever is not, is not.",
        Properties = {
            {
                Name = "Gravity",
                Type = "vector",
                Default = "vector.create(0, -9.81, 0)",
                Doc = "The acceleration applied to every unanchored part, in metres per second squared. SI and "
                    .. "signed, so the default points down; a game wanting moon gravity scales it rather than "
                    .. "reaching for a separate multiplier.",
            },
            {
                Name = "CurrentCamera",
                Type = "Camera?",
                Doc = "The camera the world is rendered from. Nil renders nothing rather than falling back to a "
                    .. "camera the engine invented, because a view nobody asked for is harder to debug than a black "
                    .. "frame that says why.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Raycast",
                Parameters = {
                    { Name = "origin", Type = "vector" },
                    { Name = "direction", Type = "vector" },
                    { Name = "params", Type = "RaycastParams?", Default = "nil" },
                },
                Returns = { "RaycastResult?" },
                ThreadSafety = "Unsafe",
                Doc = "The nearest thing a ray from `origin` hits, or nil. The direction is NOT normalised: its "
                    .. "length is how far the ray reaches, so `direction * 100` is a hundred-metre ray. A tie "
                    .. "between two surfaces at the same distance resolves the same way on every run, because a "
                    .. "query whose answer depends on traversal order is a replay divergence waiting for a body "
                    .. "count to change (R10).",
            },
            {
                Name = "Spherecast",
                Parameters = {
                    { Name = "origin", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "direction", Type = "vector" },
                    { Name = "params", Type = "RaycastParams?", Default = "nil" },
                },
                Returns = { "RaycastResult?" },
                ThreadSafety = "Unsafe",
                Doc = "The same question with a sphere swept along the ray instead of a point. This is what a "
                    .. "camera boom or a character's ground check wants: a ray slips through a gap narrower than "
                    .. "the thing it is standing in for.",
            },
            {
                Name = "GetBodiesInBox",
                Parameters = {
                    { Name = "cframe", Type = "CFrame" },
                    { Name = "size", Type = "vector" },
                    { Name = "params", Type = "RaycastParams?", Default = "nil" },
                },
                Returns = { "{BasePart}" },
                ThreadSafety = "Unsafe",
                Doc = "Every part overlapping an oriented box, as a fresh array in a stable order -- one entry "
                    .. "per part however many of its surfaces are inside. `size` is the full extent, matching "
                    .. "`BasePart.Size`.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "Lighting",
        Extends = "Instance",
        Module = "render",
        Storage = "Lighting",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "Day/night and the environment every surface is lit against. The sun is service state rather than an "
            .. "instance, because there is exactly one of it and its direction is derived rather than authored.",
        Properties = {
            {
                Name = "ClockTime",
                Type = "number",
                Doc = "The hour of day, 0 to 24, wrapping rather than clamping so that adding a delta every tick "
                    .. "never has to check. It is the ONLY input to the sun's direction, which is what makes a "
                    .. "replay light the same way the live run did.",
            },
            {
                Name = "GeographicLatitude",
                Type = "number",
                Doc = "Degrees north of the equator, which tilts the sun's arc across the sky. The second and last "
                    .. "input to SunDirection.",
            },
            {
                Name = "Ambient",
                Type = "Color3",
                Doc = "Flat light reaching every surface from every direction. A stand-in for bounced light until "
                    .. "there is any; not clamped, so it can be pushed above one deliberately.",
            },
            {
                Name = "Brightness",
                Type = "number",
                Doc = "How strong the sun is, independent of its colour.",
            },
            {
                Name = "FogColor",
                Type = "Color3",
                Doc = "What distance fades towards between FogStart and FogEnd.",
            },
            {
                Name = "FogStart",
                Type = "number",
                Doc = "Distance in metres at which fog begins.",
            },
            {
                Name = "FogEnd",
                Type = "number",
                Doc = "Distance in metres at which fog is total. Equal to or below FogStart means no fog at all, "
                    .. "which is how fog is turned off without a separate flag.",
            },
            {
                Name = "SunDirection",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "ReadParallel",
                Doc = "The unit vector pointing from the world towards the sun, derived from ClockTime and "
                    .. "GeographicLatitude and nothing else -- no wall clock and no accumulated state, so the same "
                    .. "ClockTime always gives the same direction.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "RunService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Properties = {
            {
                Name = "SimTime",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "ReadParallel",
                Doc = "The simulation clock, in seconds, at the current tick. It is constant for the whole tick -- "
                    .. "deferred drains included -- and advances by `PhysicsService.FixedTimestep` between ticks. "
                    .. "This is the clock simulation code reads: the wall clock is forbidden to it, there is no "
                    .. "`tick()`, and `os.clock` is for profiling and never for gameplay. It stops advancing while "
                    .. "the world is paused.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Pause",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Pauses the world. The simulation clock stops, so `SimTime` stops advancing and no "
                    .. "`task.wait` or `task.delay` timer comes due; rendering and the debug overlay keep running. "
                    .. "Idempotent: pausing a paused world is a no-op, not an error.",
            },
            {
                Name = "Resume",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Restarts a paused world, and its clock with it. Idempotent: resuming a running world is a "
                    .. "no-op, not an error.",
            },
            {
                Name = "IsPaused",
                Parameters = {},
                Returns = { "boolean" },
                ThreadSafety = "ReadParallel",
                Doc = "Whether the world is currently paused. Since `Pause` and `Resume` are both idempotent, this "
                    .. "is how code tells the two states apart rather than by watching a call fail.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "PreRender",
                Parameters = { { Name = "dt", Type = "number" } },
                Doc = "Fires at render rate, before the frame is drawn, carrying the variable time in seconds since "
                    .. "the previous render. It never fires in a headless run: headless is the same scheduler minus "
                    .. "the render steps, and this is one of them. It stays connectable so that shared code need "
                    .. "not branch on it -- but a handler connected to it in a headless process is a handler that "
                    .. "will not run.",
            },
            {
                Name = "PreAnimation",
                Parameters = { { Name = "dt", Type = "number" } },
                Doc = "The first phase of a sim tick, before animation is evaluated, carrying the fixed tick "
                    .. "duration in seconds.",
            },
            {
                Name = "PreSimulation",
                Parameters = { { Name = "dt", Type = "number" } },
                Doc = "Fires each sim tick after animation and before the simulation step, carrying the fixed tick "
                    .. "duration in seconds.",
            },
            {
                Name = "PostSimulation",
                Parameters = { { Name = "dt", Type = "number" } },
                Doc = "Fires each sim tick immediately after the simulation step, carrying the fixed tick duration "
                    .. "in seconds.",
            },
            {
                Name = "Heartbeat",
                Parameters = { { Name = "dt", Type = "number" } },
                Doc = "The last phase of a sim tick, carrying the fixed tick duration in seconds. It fires at the "
                    .. "sim tick rate like the three phases before it, and it is the usual place for per-tick "
                    .. "gameplay work that wants to see the state the tick settled on.",
            },
        } :: { Schema.Event },
        Doc = "The frame loop: the phase signals per-frame work hangs off, and the clock that work reads. In frame "
            .. "order they are `PreRender` at render rate with a variable delta, then `PreAnimation`, "
            .. "`PreSimulation`, `PostSimulation` and `Heartbeat` at the fixed sim tick rate. That rate split is "
            .. "the point of the service: render-rate work and simulation work are different jobs and get different "
            .. "signals.",
    },
    {
        Name = "ScriptService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        -- No members by design (§2.1): the mounted tree is the whole API.
        Doc = "The mount point for entry scripts. At boot every `src/scripts/**/*.luau` file becomes a `Script` "
            .. "child of it, with subdirectories as `Folder`s, and the `script` global inside a running entry "
            .. "script is that script's own instance here. It has no properties or methods of its own: the tree is "
            .. "the API.",
    },
    {
        Name = "TagService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Methods = {
            {
                Name = "GetTagged",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = { "{Instance}" },
                ThreadSafety = "ReadParallel",
                Doc = "Returns every instance currently carrying this tag, as a fresh array the caller owns. Tags "
                    .. "are pure instance state with no relationship to the tree, so an instance parented to `nil` "
                    .. "is returned like any other.",
            },
            {
                Name = "GetInstanceAddedSignal",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = { "Signal<Instance>" },
                ThreadSafety = "Unsafe",
                Doc = "Returns the signal that fires with each instance that gains this tag. It is deferred like "
                    .. "every signal, so a handler sees the instance at the next resumption point rather than "
                    .. "inside the `AddTag` call that added it.",
            },
            {
                Name = "GetInstanceRemovedSignal",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = { "Signal<Instance>" },
                ThreadSafety = "Unsafe",
                Doc = "Returns the signal that fires with each instance that loses this tag, whether through "
                    .. "`RemoveTag` or through `Destroy`, which strips every tag the instance carried. The fire is "
                    .. "deferred, and a destroyed handle still resolves for the drain in which it arrives, so a "
                    .. "handler can still read what it lost before the instance goes.",
            },
            {
                Name = "GetAllTags",
                Parameters = {},
                Returns = { "{string}" },
                ThreadSafety = "ReadParallel",
                Doc = "Returns the set of tags currently carried by at least one instance, as a fresh array -- not "
                    .. "every name ever seen. It updates synchronously with `AddTag` and `RemoveTag`, so a tag "
                    .. "leaves it the moment its last carrier drops it, even though the signal reporting that drop "
                    .. "is deferred like everything else.",
            },
        } :: { Schema.Method },
        Doc = "Finds instances by tag. Tags themselves are free-form string labels on an instance and are added, "
            .. "removed and tested through `Instance` -- `AddTag`, `RemoveTag`, `HasTag`, `GetTags`. What lives "
            .. "here are the queries that span the whole world, and the signals that report a tag coming and going.",
    },
    {
        Name = "DebugService",
        Extends = "Instance",
        Module = "scene",
        -- Not `DevOnly`: §2.1 keeps the service in shipped builds with the
        -- overlay off. Only the gizmo calls are a development aid, and they are
        -- no-ops rather than absent.
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Properties = {
            {
                Name = "OverlayVisible",
                Type = "boolean",
                Default = "false",
                ThreadSafety = "Unsafe",
                Doc = "Whether the debug overlay is drawn. It starts off, shipped builds included, so a game can "
                    .. "offer the overlay deliberately instead of needing a different build to get it.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "DrawLine",
                Parameters = {
                    { Name = "a", Type = "vector" },
                    { Name = "b", Type = "vector" },
                    { Name = "color", Type = "Color3?", Default = "nil" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Draws a line between two world positions for one frame. Headless there is no renderer and "
                    .. "the call is a silent no-op rather than an error, so debug drawing left in shared code "
                    .. "cannot fail a headless test.",
            },
            {
                Name = "DrawBox",
                Parameters = {
                    { Name = "cframe", Type = "CFrame" },
                    { Name = "size", Type = "vector" },
                    { Name = "color", Type = "Color3?", Default = "nil" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Draws the wireframe of a box at this frame, with these dimensions in metres, for one frame. "
                    .. "A silent no-op in a headless run, like every gizmo.",
            },
            {
                Name = "DrawSphere",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "color", Type = "Color3?", Default = "nil" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Draws the wireframe of a sphere at this world position, with this radius in metres, for one "
                    .. "frame. A silent no-op in a headless run, like every gizmo.",
            },
            {
                Name = "GetStat",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "number" },
                ThreadSafety = "ReadParallel",
                Doc = "Reads a named instrumentation counter -- `FPS`, `FrameTimeMs`, `PhysicsBodies`, "
                    .. "`InstanceCount`, `DrawCalls`, `LuaMemoryKB` and the rest, plus whatever `SetCustomStat` has "
                    .. "published. An unregistered name raises `scene.err.unknown_stat` instead of returning zero: "
                    .. "a misspelt stat is a bug in the caller, and a debug surface that answers zero hides that "
                    .. "bug in the one place people are already confused.",
            },
            {
                Name = "SetCustomStat",
                Parameters = {
                    { Name = "name", Type = "string" },
                    { Name = "value", Type = "number" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Publishes a game-defined number under this name, readable afterwards through `GetStat` and "
                    .. "shown on the stats panel. A value that is not a number raises like any other typed-argument "
                    .. "mismatch.",
            },
            {
                Name = "ShowPanel",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Opens one panel of the overlay by name. The built-ins are `Stats`, `Scene`, `Log`, "
                    .. "`Streaming` and `Physics`; an unknown panel raises `scene.err.unknown_stat`.",
            },
            {
                Name = "HidePanel",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Closes one panel of the overlay by name, taking the same names as `ShowPanel`. An unknown "
                    .. "panel raises `scene.err.unknown_stat`.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "MessageOut",
                Parameters = {
                    { Name = "message", Type = "string" },
                    { Name = "level", Type = "Enum.LogLevel" },
                },
                Doc = "Fires for every console message, carrying the text and its level. `print` and `warn` each "
                    .. "produce exactly one deferred fire with their text verbatim, at `Info` and `Warning`. Engine "
                    .. "messages arrive pre-formatted with their key in front, as in `[scene.err.parent_cycle] "
                    .. "...`, so a handler matching on a key substring sees engine output while one matching prose "
                    .. "sees script output. Contained handler errors and the re-entrancy cap's dropped-fire log come "
                    .. "through here too.",
            },
        } :: { Schema.Event },
        Doc = "The debug overlay and the engine's own instrumentation. The service is present in shipped builds "
            .. "with the overlay off unless a game turns it on, so the panels, the stats and the log stream are "
            .. "available to a shipped game that wants them. The gizmo calls are a development aid and draw nothing "
            .. "at all in a headless run.",
    },
    {
        Name = "HotReloadService",
        Extends = "Instance",
        Module = "scene",
        -- `DevOnly`: api-design.md §2.1 keeps this service out of shipped
        -- builds entirely, which is the one honest answer for a service whose
        -- whole subject is a development loop.
        Tags = { "Service", "NotCreatable", "DevOnly" } :: { Schema.ClassTag },
        Methods = {
            {
                Name = "SaveState",
                Parameters = {
                    { Name = "key", Type = "string" },
                    { Name = "value", Type = "any" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Stores a value under `key` so the world built by the next reload can read it back. The value "
                    .. "is converted to an engine-side copy at the moment of the call, because the VM holding the "
                    .. "original is what a reload destroys -- so nil, booleans, numbers, strings, buffers, and "
                    .. "tables of those are accepted, while a function, a thread, an Instance or a cyclic table "
                    .. "raises `script.err.unsavable_state`. Raising beats dropping: a reload that quietly loses "
                    .. "state is worse than one that says what it could not keep.",
            },
            {
                Name = "LoadState",
                Parameters = { { Name = "key", Type = "string" } },
                Returns = { "any" },
                ThreadSafety = "Unsafe",
                Doc = "Reads back what `SaveState` stored under `key` before the reload, or nil if nothing did. "
                    .. "The value is a fresh copy built in this VM; mutating it changes nothing that a later "
                    .. "reload will see, and only another `SaveState` does.",
            },
            {
                Name = "IsReload",
                Parameters = {},
                Returns = { "boolean" },
                ThreadSafety = "Safe",
                Doc = "Whether this world was built by a hot reload rather than by starting the engine. It is how "
                    .. "a script knows to look for what it left behind instead of building it again -- the "
                    .. "instances tagged `PreserveOnReload` are already in the tree by the time any entry script "
                    .. "runs.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "PreReload",
                Parameters = {},
                Doc = "Fires on the outgoing world when a reload has been requested and before anything is torn "
                    .. "down, which is the last moment `SaveState` can be called and have the value survive. Its "
                    .. "handlers are drained before the new world is built, so the reload waits for them.",
            },
            {
                Name = "PostReload",
                Parameters = {},
                Doc = "Fires on the world a reload just built, after every entry script has had its first "
                    .. "resumption and the preserved state is already in place. A handler connected at file scope "
                    .. "therefore sees a world that is finished rather than one still being assembled.",
            },
        } :: { Schema.Event },
        Doc = "The hot-reload loop as a script can see it (ADR 0024). Present in development builds only: a "
            .. "shipped game has no reload to observe, and a service that existed there and never fired would be "
            .. "a documented no-op. A reload rebuilds the world from source and keeps nothing by default -- what "
            .. "survives is what `SaveState` stored and what was tagged `PreserveOnReload`.",
    },
    {
        -- **A scaffold, and tagged so that it cannot outlive its milestone.**
        -- The roadmap asks M5 for "minimal direct keyboard polling,
        -- deliberately replaced in M6 by the Input Action System", and ADR 0029
        -- says the IAS is the ONLY input model. Both are true at once only if
        -- this is visibly temporary, so it is `DevOnly` -- compiled out of a
        -- shipping build -- which makes its removal structural rather than a
        -- promise in a comment.
        --
        -- The key is a STRING rather than an `Enum.KeyCode`. `Enum.KeyCode` is
        -- a v1 enum whose full list (keys, mouse, gamepad) belongs to the IAS
        -- at M6, where it is designed once with the whole list in front of it;
        -- shipping a third of it here would fix item values for a surface that
        -- has not been designed. A stringly-typed scaffold also cannot be
        -- mistaken for the real API.
        Name = "KeyboardService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable", "DevOnly" } :: { Schema.ClassTag },
        Methods = {
            {
                Name = "IsKeyDown",
                Parameters = { { Name = "key", Type = "string" } },
                Returns = { "boolean" },
                ThreadSafety = "Unsafe",
                Doc = 'Whether a key is held right now, named by its US-layout legend: "W", "Space", '
                    .. '"LeftShift". A name no key carries is false rather than an error, because a scaffold '
                    .. "that raises on a typo teaches nothing the IAS will not re-teach. Reads the snapshot the "
                    .. "current simulation tick was given, so two reads inside one tick agree and a replay "
                    .. "reproduces both.",
            },
        } :: { Schema.Method },
        Doc = "Direct keyboard polling, for M5 and M5 only. It exists because a character has to be steerable "
            .. "before the Input Action System lands, and it is gone the moment it does (M6). Nothing built on "
            .. "it survives; that is the point of the migration being a gate item.",
    },
    {
        Name = "PhysicsService",
        Extends = "Instance",
        -- `scene` rather than a physics module, because in this release the one
        -- member it carries is scheduler state that physics merely names -- M2
        -- brief, Decision 6 as amended.
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Properties = {
            {
                Name = "FixedTimestep",
                Type = "number",
                Default = "1/60",
                ThreadSafety = "ReadParallel",
                Doc = "The duration of one simulation tick, in seconds, and therefore the grid every timing "
                    .. "guarantee is expressed against: `task.wait(1)` is exactly 60 ticks at the default 1/60, on "
                    .. "every machine and every run. Express durations as multiples of it and the same code stays "
                    .. "correct at 30 Hz or at 240 Hz. Writable from M5, and a write takes effect at the next "
                    .. "frame start rather than mid-tick: the accumulator, the timer wheel and the solver all "
                    .. "read it, and changing it between two of those reads inside one frame is a class of bug "
                    .. "worth designing out. Values outside 1/240 to 1/30 are refused.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "RegisterCollisionGroup",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Declares a collision group. Registering a name that already exists is a no-op rather than "
                    .. "an error, which is what lets a script register its groups at file scope and survive a hot "
                    .. "reload. The group collides with everything until told otherwise.",
            },
            {
                Name = "CollisionGroupSetCollidable",
                Parameters = {
                    { Name = "a", Type = "string" },
                    { Name = "b", Type = "string" },
                    { Name = "collidable", Type = "boolean" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Sets whether parts in two groups collide with each other. Symmetric: setting a against b "
                    .. "sets b against a, because a one-way collision is not a thing a solver can express.",
            },
            {
                Name = "GetRegisteredCollisionGroups",
                Parameters = {},
                Returns = { "{string}" },
                ThreadSafety = "Unsafe",
                Doc = "Every registered group name, in registration order, `Default` first. A fresh array on "
                    .. "every call, and ordered rather than hashed so that iterating it cannot leak a container's "
                    .. "own order into a simulation (R10).",
            },
        } :: { Schema.Method },
        Doc = "The simulation tick grid and the physics controls that do not belong on an individual part. It "
            .. "shipped before any physics did because `FixedTimestep` is a scheduler property that physics merely "
            .. "names, and everything about scheduling and timing is expressed against it.",
    },
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
