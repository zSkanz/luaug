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
                -- A fact about the build, not about the world.
                HostFact = true,
                Doc = "The version of the engine runtime executing this VM, as `luaug.toml` pins it. Read-only: "
                    .. "the runtime is chosen before the VM exists, so nothing a script does can change it.",
            },
            {
                Name = "LuauVersion",
                Type = "string",
                ReadOnly = true,
                ThreadSafety = "Safe",
                -- A fact about the build, not about the world.
                HostFact = true,
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
                Doc = "Returns the service of this name. Services are singletons and all of them exist from boot, so "
                    .. "this is a lookup rather than a creation and every call returns the same ordinary child of "
                    .. "`game`. A string literal gives an exactly typed result; a name computed at runtime gives "
                    .. "the general `Instance`, which is the honest cost of not knowing the name at analysis time. "
                    .. "An unknown name raises `scene.err.unknown_service`.",
            },
            {
                Name = "FindService",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Instance?" },
                ThreadSafety = "ReadParallel",
                Doc = "Returns the service of this name, or `nil` for a name that is not a service at all. "
                    .. "\n\n**It answers the same as `GetService` for every real service now**, and that is a "
                    .. 'narrowing worth stating: it used to mean "is this service in use", because a service was '
                    .. "created by its first `GetService`. All of them exist from boot, so there is no such "
                    .. "question left to ask. What is left is the difference in how they refuse an unknown name -- "
                    .. "this one returns `nil` where `GetService` raises -- which is the reason to keep it.",
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
        Doc = "The root of the instance tree, reached through the `game` global. **Every service is a child of it and "
            .. "every service exists from boot**, in the order this file declares them -- so the tree has the same "
            .. "shape before a line of script has run as it does after, which is what lets an editor show it. It is "
            .. "the services' parent rather than one of them.",
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
                ErrorKey = "scene.err.number_latitude_degrees",
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
                ErrorKey = "scene.err.number_at_least_zero",
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
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Distance in metres at which fog begins.",
            },
            {
                Name = "FogEnd",
                Type = "number",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Distance in metres at which fog is total. Equal to or below FogStart means no fog at all, "
                    .. "which is how fog is turned off without a separate flag.",
            },
            {
                Name = "ExposureCompensation",
                Type = "number",
                ErrorKey = "scene.err.number_exposure_stops",
                Doc = "Exposure in EV stops, on top of the automatic exposure the renderer measures from the frame "
                    .. "itself. Zero means whatever it measured; +1 is twice the light and -1 is half, which is the "
                    .. "unit a camera uses. It is a look rather than a limit, so it is not clamped.",
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
        Name = "StreamingService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                ThreadSafety = "ReadParallel",
                Doc = "Whether the world streams at all. Turning it off FREEZES the current set rather than "
                    .. "draining it: nothing new is scheduled and nothing resident is evicted, which is what "
                    .. "makes it usable as `hold the world still while I do something`. Evicting while refusing "
                    .. "to load would empty the world one ring at a time and leave nothing to come back to.",
            },
            {
                Name = "LoadRadius",
                Type = "number",
                Default = "1024",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_positive",
                Doc = "How far from a focus the engine tries to keep chunks resident, in metres. Best effort: a "
                    .. "chunk inside it is wanted, and one outside stays resident until it passes a WIDER ring, "
                    .. "because a single radius makes a focus standing on a boundary load and evict the same "
                    .. "chunk every frame -- and the symptom of that is not a wrong world, it is a stutter "
                    .. "nobody can find.",
            },
            {
                Name = "MinRadius",
                Type = "number",
                Default = "512",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_positive",
                Doc = "The must-have ring, in metres. Everything inside it is guaranteed resident before a focus "
                    .. "may advance into it, so the character walks into a world that exists rather than "
                    .. "through one that has not arrived. Read `PauseOutsideLoadedArea` for what happens when it "
                    .. "has not.",
            },
            {
                Name = "PauseOutsideLoadedArea",
                Type = "boolean",
                Default = "false",
                ThreadSafety = "ReadParallel",
                Doc = "Whether the simulation waits when the minimum ring is not yet resident. A familiar "
                    .. "streaming pause, and the alternative is a character falling through ground that has not "
                    .. "loaded. It is a pause and not a rollback: the tick simply does not advance.\n\n**In a "
                    .. "world that is not the authority it does nothing.** A copy of a shared world cannot stop "
                    .. "a simulation it does not own; what it does instead is hold its camera and say that it "
                    .. "is loading.",
            },
            {
                Name = "StructureMinRadius",
                Type = "number",
                Default = "0",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The must-have ring for cells of STRUCTURES, in metres. A mountain and a pebble stop "
                    .. "sharing a radius, which is the choice one grid forces and always resolves badly in one "
                    .. "direction.\n\n**Zero means follow `MinRadius`**, which is the default and is why a world "
                    .. "authored before this existed behaves exactly as it did: everything it holds is detail, "
                    .. "and detail is what `MinRadius` and `LoadRadius` have always meant.",
            },
            {
                Name = "StructureLoadRadius",
                Type = "number",
                Default = "0",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far from a focus cells of STRUCTURES are kept, in metres. Best effort and hysteretic, "
                    .. "exactly as `LoadRadius` is. Zero means follow `LoadRadius`.",
            },
            {
                Name = "TerrainMinRadius",
                Type = "number",
                Default = "0",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The must-have ring for cells of TERRAIN FEATURES, in metres. The longest of the three: it "
                    .. "is what puts a horizon further out than the props standing on it. Zero means follow "
                    .. "`MinRadius`.",
            },
            {
                Name = "TerrainLoadRadius",
                Type = "number",
                Default = "0",
                ThreadSafety = "ReadParallel",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far from a focus cells of TERRAIN FEATURES are kept, in metres. Zero means follow "
                    .. "`LoadRadius`.\n\nRaising this is the cheapest distance there is, and it is cheap for a "
                    .. "reason worth knowing: a cell of terrain holds few large things, so a ring twice as wide "
                    .. "costs far less than the same ring would cost in props.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "AddFocus",
                Parameters = { { Name = "instance", Type = "Instance" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Streams the world around this instance as well. A focus is normally the character or the "
                    .. "camera; several are legal and the engine keeps a chunk that any of them wants. An "
                    .. "instance with no position in the world is refused, because a focus that cannot be "
                    .. "located would silently anchor the world at the origin.",
            },
            {
                Name = "RemoveFocus",
                Parameters = { { Name = "instance", Type = "Instance" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Stops streaming around it. Removing the last focus does not unload the world: it stops "
                    .. "anything being wanted, and what is resident stays until something wants it gone.",
            },
            {
                Name = "LoadAreaAsync",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "radius", Type = "number" },
                },
                Returns = {},
                Yields = true,
                ThreadSafety = "Unsafe",
                Doc = "Yields until every chunk within `radius` of `position` is resident. What a teleport calls "
                    .. "before it moves the character, so the destination exists when they arrive. Returns "
                    .. "immediately when the area is already loaded, and raises rather than hanging when the "
                    .. "world has no chunks there at all.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "AreaLoaded",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "radius", Type = "number" },
                },
                Doc = "Fires when an area a script asked for has finished loading. Deferred like every signal.",
            },
            {
                Name = "InstanceStreamedOut",
                Parameters = { { Name = "instance", Type = "Instance" } },
                Doc = "Fires for each instance that left the world while a script still held a reference to it. "
                    .. "Streamed out means REPARENTED TO NIL rather than destroyed, so the handle a script holds "
                    .. "still resolves -- it is a husk, and reading it is legal. An instance nothing referenced "
                    .. "is simply destroyed and does not fire this.",
            },
        } :: { Schema.Event },
        Doc = "The streamed world's controls (api-design.md §2.1). A world too large to hold is divided into a "
            .. "uniform grid of chunks; this service decides which of them are resident, by distance to the foci "
            .. "a game names. Streaming is a system rather than scene-root state, which is why it is a service "
            .. "and not a property of `Workspace`. The per-frame cost is budgeted in TIME rather than in a count "
            .. "of chunks, because a chunk's cost varies with what is in it.",
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
                    .. "`InstanceCount`, `DrawCalls`, `MeshLodDraws`, `LuaMemoryKB` and the rest, plus whatever `SetCustomStat` has "
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
                ErrorKey = "scene.err.number_fixed_timestep",
                Doc = "The duration of one simulation tick, in seconds, and therefore the grid every timing "
                    .. "guarantee is expressed against: `task.wait(1)` is exactly 60 ticks at the default 1/60, on "
                    .. "every machine and every run. Express durations as multiples of it and the same code stays "
                    .. "correct at 30 Hz or at 240 Hz. Writable, and a write takes effect at the next "
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
    {
        Name = "InputService",
        Extends = "Instance",
        Module = "input",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "The host of the Input Action System (§2.4, ADR 0029) and the only place device-wide state is "
            .. "readable. There is no UserInputService, no ContextActionService and no Mouse: one input model, "
            .. "rebindable and promptable by default (divergence #3).",
        Properties = {
            {
                Name = "PointerLocked",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the pointer is locked to the window and reporting motion instead of position -- "
                    .. "what a first-person camera wants. While it is locked, `GetPointerPosition` keeps "
                    .. "reporting the position the pointer had when it was locked, because there is no other "
                    .. "honest answer, and a `Direction2D` action bound to `MouseMovement` is the way to read "
                    .. "the motion.",
            },
            {
                Name = "PointerVisible",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the system cursor is drawn. Independent of `PointerLocked`, because the two are "
                    .. "separate wishes: a strategy game hides the cursor during a cutscene without locking it.",
            },
            {
                Name = "LastInputDeviceType",
                Type = "Enum.InputDeviceType",
                ReadOnly = true,
                Doc = "The device family the player most recently used, which is what a HUD switches its "
                    .. "prompts on. It changes on a real input and not on a resting one: a gamepad stick "
                    .. "drifting inside its dead zone does not steal the prompts from a keyboard.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "GetPointerPosition",
                Parameters = {},
                Returns = { "Vector2" },
                Doc = "The pointer in window pixels, origin at the top left, as of the current tick. A "
                    .. "snapshot like every other input read: two calls inside one tick agree.",
            },
            {
                Name = "IsKeyDown",
                Parameters = { { Name = "keyCode", Type = "Enum.KeyCode" } },
                Returns = { "boolean" },
                Doc = "Whether that key, button or trigger is held as of the current tick. The shortest thing "
                    .. "this API can be asked, and the reason there is no separate sugar module for "
                    .. "it.\n\nIt reads the device SNAPSHOT and ignores what the UI consumed, which is the "
                    .. "opposite of the events' second argument -- a poll asks what the hardware is doing and "
                    .. "an event asks what happened to the game. A caller who wants the UI-aware answer wants "
                    .. "an `InputAction`, which is also the one that can be rebound.\n\nAn analogue source "
                    .. "counts as down past half deflection, which is the same rule a `Bool` action applies to "
                    .. "one.",
            },
            {
                Name = "SetVirtualState",
                Parameters = {
                    { Name = "keyCode", Type = "Enum.KeyCode" },
                    { Name = "value", Type = "number" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Drives one of the `Virtual` key codes from something that is not hardware -- a HUD "
                    .. "button, an on-screen thumbstick, an accessibility control for a player who cannot hold "
                    .. "a key (§2.4).\n\n**It writes into the same device snapshot a keyboard writes into**, "
                    .. "which is what makes it one input model rather than two: the value binds through an "
                    .. "ordinary `InputBinding`, resolves in the ordinary order, is eaten by an ordinary "
                    .. "sinking context, and is carried by the recorded stream a replay hands back. A seam "
                    .. "that reached past the snapshot would be a HUD button a recorded stream could not "
                    .. "see.\n\n**It carries a value, not a press.** Write 1 and 0 for a button; write anything "
                    .. "between for a slider or the axis of a thumbstick. Bound to a `Bool` action the value "
                    .. "counts as pressed past half deflection, which is the rule every analogue source "
                    .. "follows.\n\nThe value STICKS until it is written again -- a button that is held is a "
                    .. "1 nobody has cleared -- and losing window focus clears it with everything else, so a "
                    .. "press cannot survive an alt-tab. Refuses any key code that is not one of the `Virtual` "
                    .. "family: writing to `Space` would be a script pretending to be a keyboard.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "InputBegan",
                Parameters = {
                    { Name = "input", Type = "InputObject" },
                    { Name = "uiConsumed", Type = "boolean" },
                },
                Doc = "Fired on the tick an input STARTS -- a key pressed, a button pushed, a trigger crossing "
                    .. "half deflection (§2.4, ADR 0041).\n\n**`uiConsumed` is the second argument for a "
                    .. "reason: read it.** It is true when the interface already took this input -- the "
                    .. "pointer was over an element, or a `TextInput` had focus -- and a handler that ignores "
                    .. "it is a handler that fires the gun when the player clicks a button and jumps when they "
                    .. "type a `w` into a chat box.\n\n**What this costs, stated where it is read: a key "
                    .. "handled here cannot be rebound.** It does not appear in a remapping screen, because "
                    .. "nothing in the tree says it exists. An `InputAction` does appear, binds a keyboard key "
                    .. "and a gamepad button to one name, and is what a shipped game should use -- this is the "
                    .. "direct, familiar option beside it, and it is the right one for a prototype, a debug "
                    .. "key, or anything a player will never remap.\n\nIt fires on the SIMULATION clock, from "
                    .. "the Input Action System's own dispatch, so a handler that writes to the world "
                    .. "replays exactly. Camera look at render rate is an `InputContext` with "
                    .. "`Rate = Enum.InputRate.Render`.",
            },
            {
                Name = "InputChanged",
                Parameters = {
                    { Name = "input", Type = "InputObject" },
                    { Name = "uiConsumed", Type = "boolean" },
                },
                Doc = "Fired on a tick an input MOVED without starting or ending: pointer motion, the wheel, a "
                    .. "stick or a trigger's deflection. `Position` and `Delta` carry it.\n\nOne fire per tick "
                    .. "per source at most, with the deltas ACCUMULATED since the last one -- several device "
                    .. "events land in a tick and a handler that saw only the last would lose most of a fast "
                    .. "flick. The same rebinding cost as `InputBegan` applies.",
            },
            {
                Name = "InputEnded",
                Parameters = {
                    { Name = "input", Type = "InputObject" },
                    { Name = "uiConsumed", Type = "boolean" },
                },
                Doc = "Fired on the tick an input STOPS. It fires for everything held when the window loses "
                    .. "focus, so a handler that pairs `InputBegan` with this one never leaks a press -- which "
                    .. "is the failure that leaves a character walking into a wall after an alt-tab.\n\n"
                    .. "`uiConsumed` carries what it carried when the input began, so a press that started on "
                    .. "a button is still marked consumed when it is released off one.",
            },
            {
                Name = "InputDeviceChanged",
                Parameters = { { Name = "deviceType", Type = "Enum.InputDeviceType" } },
                Doc = "Fired when `LastInputDeviceType` changes, so a prompt redraws once rather than polling.",
            },
            {
                Name = "WindowFocusChanged",
                Parameters = { { Name = "focused", Type = "boolean" } },
                Doc = "Fired when the game window gains or loses keyboard focus. Losing focus releases every "
                    .. "held input, so an alt-tab does not leave a character walking into a wall.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "TweenService",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "Property animation (§2.1). It exists so that "
            .. '"move this over half a second, easing out" is one call rather than a `Heartbeat` handler '
            .. "with a timer in it -- and so that the engine, not the game, owns the arithmetic that makes "
            .. "two such animations agree.",
        Methods = {
            {
                Name = "Create",
                Parameters = {
                    { Name = "instance", Type = "Instance" },
                    { Name = "tweenInfo", Type = "TweenInfo" },
                    { Name = "goals", Type = "{ [string]: any }" },
                },
                Returns = { "Tween" },
                ThreadSafety = "Unsafe",
                Doc = "A tween that will move each named property of `instance` to its goal. Created stopped: "
                    .. "`Play` starts it.\n\nEvery goal is validated NOW rather than at the first write -- a "
                    .. "property the class does not have, a read-only one, or a value of the wrong type raises "
                    .. "here, where the caller is. The alternative is a tween that plays for half a second and "
                    .. "then reports a typo. Only properties the engine can interpolate are accepted: numbers, "
                    .. "vectors, CFrames, Color3s, Vector2s, UDims, UDim2s and Rects. A boolean or a string "
                    .. "goal raises, because there is no halfway between two of either.",
            },
            {
                Name = "GetValue",
                Parameters = {
                    { Name = "alpha", Type = "number" },
                    { Name = "easingStyle", Type = "Enum.EasingStyle" },
                    { Name = "easingDirection", Type = "Enum.EasingDirection" },
                },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The eased value of `alpha` under that curve -- the same arithmetic a tween uses, "
                    .. "exposed so a game can ease something the tween system does not own. `alpha` is not "
                    .. "clamped, and several styles leave 0-1 on purpose (`Back`, `Elastic`).",
            },
        } :: { Schema.Method },
    },
    {
        Name = "UIService",
        Extends = "Instance",
        Module = "ui",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "The parent of every `ScreenGui` and the source of the two numbers a layout needs about the "
            .. "screen it is on (§2.1). It takes the PlayerGui role without taking the Player: there is no "
            .. "`Players` service in v1 and a UI does not need one.",
        Properties = {
            {
                Name = "SafeAreaInsets",
                Type = "Rect",
                ReadOnly = true,
                Transient = true,
                -- A fact about the window, not about the world.
                HostFact = true,
                Doc = "How far in from each window edge it is safe to draw, in pixels -- what a notch, a "
                    .. "rounded corner or a system gesture bar takes away. Zero on a desktop window, which is "
                    .. "why a HUD that ignores it looks fine until it does not.",
            },
            {
                Name = "DisplayScale",
                Type = "number",
                ReadOnly = true,
                Transient = true,
                -- A fact about the display, not about the world. A trace recorded
                -- on a doubled screen must replay on an ordinary one.
                HostFact = true,
                Doc = "The window's pixel density relative to its logical size: 2 on a doubled display. UI "
                    .. "coordinates are in PIXELS, so this is what a game multiplies by when it wants a "
                    .. "measurement to mean the same physical size on two screens.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "AudioService",
        Extends = "Instance",
        Module = "audio",
        Tags = { "Service", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "Mixing and the listener (§2.1). **The listener is `Workspace.CurrentCamera`** and is not "
            .. "settable: a game with two ideas about where the player is hearing from is a game with a bug, "
            .. "and the camera is already the one thing that answers that question.",
        Properties = {
            {
                Name = "MasterVolume",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Multiplied into every sound, after its own volume and its group's. The one a settings "
                    .. "screen writes.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "PlayLocal",
                Parameters = { { Name = "content", Type = "Content" } },
                Returns = { "Sound" },
                ThreadSafety = "Unsafe",
                Doc = "Creates a 2D `Sound` parented to this service and plays it. For the fire-and-forget "
                    .. "case -- a click, a pickup -- where naming an instance is all ceremony.\n\n**It does "
                    .. "not clean up after itself in this release**: the `Sound` stays a child of the service "
                    .. "once it has ended, so a caller firing one per frame accumulates them. Keep the "
                    .. "returned handle and `Destroy` it on `Ended` where that matters. The handle is also "
                    .. "what a caller sets the volume through, since this takes only the content.",
            },
        } :: { Schema.Method },
    },
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
