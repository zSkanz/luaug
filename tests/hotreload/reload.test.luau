--!strict
--[[
	The M3 gate's first item, end to end:

		"automated E2E hot-reload test (dev server started headless, file
		mutated by the test, WebSocket confirms reload, behavior change
		asserted via world hash)"

	Every one of those clauses is a line below. The dev server is started for
	real, the engine is a child process that dials into it over WebSocket (ADR
	0035), the test connects as an ordinary observer client, and what confirms
	the reload is a `reloaded` message arriving over that socket.

	The assertion is the strong form (M3 brief, Decision 11): the world after a
	reload must hash the same as a COLD BOOT of the edited source at the same
	tick, and differently from the world before the edit. Either half alone is
	satisfied by a reload that did nothing -- "the hash changed" passes against
	one that half-applied the edit, and "it matches a cold boot" passes against
	one that never ran.

	The server is driven directly rather than through `luaug dev`, for one
	reason: `devserver.start` can bind port 0 and be told which port it got,
	while a spawned CLI would need a port chosen in advance and would fail on a
	machine where something else already had it. `luaug dev` is a thin wrapper
	over exactly this call; what it adds is argument handling and logging.
]]

local assert = require("@std/test/assert")
local fs = require("@std/fs")
local json = require("@std/json")
local net = require("@lute/net")
local process = require("@std/process")
local task = require("@std/task")
local test = require("@std/test")

local devserver = require("../../tools/cli/devserver")
local projectlib = require("../../tools/cli/project")

-- ADR 0024's hard requirement. Measured engine-side, from the FrameStart safe
-- point through the new world being live (M3 brief, Decision 10) -- not from
-- this file's `writeStringToFile`, which would also be measuring the
-- filesystem, the debounce and two process schedulers.
local ReloadBudgetMs = 500

-- The tick both worlds are sampled at. Large enough that the Heartbeat handler
-- below has run many times, so the hash reflects simulated behaviour rather
-- than the tree boot built -- and far enough ahead in wall-clock terms (a dev
-- session runs at the fixed 60 Hz tick, so this is one second) that the request
-- for it always arrives before the world gets there.
local SampleTick = 60

-- A long string rather than an interpolated one: Luau's backtick strings cannot
-- span lines, and this has to be a whole file.
local ScriptTemplate = [[
--!strict
local RunService = game:GetService("RunService")

local Spinner = Instance.new("Part")
Spinner.Name = "Spinner"
Spinner.Parent = workspace

local Speed = __SPEED__
local elapsed = 0

RunService.Heartbeat:Connect(function(delta: number)
    elapsed += delta
    Spinner.Position = vector.create(elapsed * Speed, 0, 0)
end)
]]

local function scriptWithSpeed(speed: string): string
    return (string.gsub(ScriptTemplate, "__SPEED__", speed))
end

-- A project the size of `examples/01-instances`: five Models of a hundred Parts,
-- each with a name, a colour and a position, plus a Heartbeat that moves them.
-- The one-script project above measures the mechanism; this measures the number
-- ADR 0024's budget is actually about, which is a reload of something real.
local HeavyTemplate = [[
--!strict
local RunService = game:GetService("RunService")

local ModelCount = 5
local PerModel = 100

local Root = Instance.new("Folder")
Root.Name = "Rings"
Root.Parent = workspace

local Cubes: { Part } = table.create(ModelCount * PerModel)
for modelIndex = 1, ModelCount do
    local model = Instance.new("Model")
    model.Name = `Ring{modelIndex}`
    model.Parent = Root

    for cubeIndex = 1, PerModel do
        local cube = Instance.new("Part")
        cube.Name = `Cube{cubeIndex}`
        cube.Size = vector.create(0.13, 0.13, 0.13)
        cube.Color = Color3.fromRGB(40 * modelIndex % 255, 90, 200)
        cube.Parent = model
        table.insert(Cubes, cube)
    end
end

local Speed = __SPEED__
local phase = 0

RunService.Heartbeat:Connect(function(delta: number)
    phase += delta * Speed
    for index, cube in Cubes do
        local angle = phase + index * 0.05
        cube.Position = vector.create(math.cos(angle) * 6, 0, math.sin(angle) * 6)
    end
end)
]]

local function heavyScript(speed: string): string
    return (string.gsub(HeavyTemplate, "__SPEED__", speed))
end

type Session = {
    server: devserver.Handle,
    socket: net.WebSocket,
    inbox: { { [string]: any } },
    close: () -> (),
}

--- Starts the dev server on a port the OS chooses, launches the engine headless
--- against it, and connects one observer client.
local function startSession(root: string): Session
    local engine = projectlib.findEngine()
    assert.that(engine ~= nil, "luaug-host was not found; build the engine or set LUAUG_HOST")

    local token = devserver.mintToken()
    local server = devserver.start({ root = root, port = 0, token = token })

    task.spawn(function()
        process.run({
            engine :: string,
            root,
            `--dev-control=ws://127.0.0.1:{server.port}/control`,
            `--dev-token={token}`,
            "--headless",
            "--rhi=null",
        }, { stdio = "none" })
    end)

    local inbox: { { [string]: any } } = {}
    local socket = net.client.websocket(`ws://127.0.0.1:{server.port}/control`, {
        onmessage = function(message)
            local text = if typeof(message) == "string" then message else buffer.tostring(message :: buffer)
            local decoded = json.asObject(json.deserialize(text))
            if decoded ~= nil then
                table.insert(inbox, decoded :: any)
            end
        end,
    })

    return {
        server = server,
        socket = socket,
        inbox = inbox,
        close = function()
            -- Through the protocol rather than by killing the process: a dev
            -- session that cannot be asked to stop is one that leaves engines
            -- behind on a CI runner, which is invisible until the runner runs
            -- out of something (M3 brief, entering risk 4).
            --
            -- Closing the server is the belt to that braces: a headless engine
            -- whose control connection drops exits by itself, which covers the
            -- case where it never attached and never heard the shutdown.
            server:sendToEngine({ type = "shutdown", id = server:nextRequestId() })
            task.wait(0.3)
            socket:close()
            server:close()
            task.wait(0.2)
        end,
    }
end

--- Waits until `predicate` finds a message in the inbox, or fails.
local function waitFor(session: Session, what: string, predicate: ({ [string]: any }) -> boolean): { [string]: any }
    for _ = 1, 600 do
        for _, message in session.inbox do
            if predicate(message) then
                return message
            end
        end
        task.wait(0.05)
    end
    error(`timed out waiting for {what}`)
end

--- Asks the engine for the world hash at an absolute tick. Absolute is the
--- whole point: a reload restarts the counter, so "tick 40" is the one thing
--- that means the same in a reloaded world and in a cold boot of the same
--- source (M3 brief, Decision 11).
local function sampleAt(session: Session, tick: number): { [string]: any }
    local id = session.server:nextRequestId()
    assert.that(session.server:sendToEngine({ type = "sample", id = id, atTick = tick }), "the engine is attached")
    local sample = waitFor(session, `a sample for request {id}`, function(message)
        return message.type == "sample" and message.id == id
    end)
    assert.eq(sample.tick, tick)
    return sample
end

local function temporaryProject(name: string): string
    local root = projectlib.joined(process.env.TMPDIR or process.env.TEMP or "/tmp", `luaug-hotreload-{name}`)
    if fs.exists(root) then
        fs.removeDirectory(root, { recursive = true })
    end
    fs.createDirectory(projectlib.joined(root, "src", "scripts"), { makeParents = true })
    return root
end

test.suite("hot reload", function(suite)
    suite:case("a saved file rebuilds the world, and the WebSocket says so", function()
        local root = temporaryProject("edit")
        local entry = projectlib.joined(root, "src", "scripts", "main.luau")
        fs.writeStringToFile(entry, scriptWithSpeed("1"))

        local session = startSession(root)

        -- The engine announces itself before anything else can be asked of it.
        local hello = waitFor(session, "the engine to attach", function(message)
            return message.type == "hello"
        end)
        assert.eq(hello.role, "engine")
        assert.eq(hello.protocol, 1)

        local before = sampleAt(session, SampleTick)

        -- The mutation. Everything after this is the loop under test.
        fs.writeStringToFile(entry, scriptWithSpeed("7"))

        -- "WebSocket confirms reload": this message arrived over the socket the
        -- engine dialled out on, relayed to this observer verbatim.
        local reloaded = waitFor(session, "the reload to complete", function(message)
            return message.type == "reloaded"
        end)
        assert.that(reloaded.ok == true, `the reload failed: {tostring(reloaded.detail)}`)
        assert.eq(reloaded.scripts, 1)

        -- ADR 0024's budget, on the span the engine measured. Printed as well
        -- as asserted: a budget nobody reports is a budget that silently drifts
        -- to just under its ceiling, and this line is where the number in
        -- `docs/perf-baselines.md` comes from.
        local ms = tonumber(reloaded.ms) or math.huge
        print(`    reload span: {string.format("%.1f", ms)} ms (budget {ReloadBudgetMs} ms)`)
        assert.that(ms < ReloadBudgetMs, `the reload took {ms} ms, over the {ReloadBudgetMs} ms budget`)

        local after = sampleAt(session, SampleTick)
        session.close()

        -- Half of the assertion: the edit took effect.
        assert.neq(after.hash, before.hash)

        -- The other half, and the one that matters: a reloaded world is
        -- indistinguishable from one that never reloaded. Booted cold from the
        -- source as it now stands, ticked to the same point.
        local coldRoot = temporaryProject("cold")
        fs.writeStringToFile(projectlib.joined(coldRoot, "src", "scripts", "main.luau"), scriptWithSpeed("7"))

        local cold = startSession(coldRoot)
        waitFor(cold, "the engine to attach", function(message)
            return message.type == "hello"
        end)
        local coldSample = sampleAt(cold, SampleTick)
        cold.close()

        assert.eq(after.hash, coldSample.hash)
    end)

    suite:case("a file that will not compile leaves the running world alone", function()
        local root = temporaryProject("broken")
        local entry = projectlib.joined(root, "src", "scripts", "main.luau")
        fs.writeStringToFile(entry, scriptWithSpeed("1"))

        local session = startSession(root)
        waitFor(session, "the engine to attach", function(message)
            return message.type == "hello"
        end)
        sampleAt(session, SampleTick)

        fs.writeStringToFile(entry, "this is not valid Luau ((")

        local reloaded = waitFor(session, "the reload to be refused", function(message)
            return message.type == "reloaded"
        end)
        assert.eq(reloaded.ok, false)

        -- The session survived, which is the whole point of building the new
        -- world before destroying the old one. Asserted by asking for a tick
        -- further on and getting it: a world that had been torn down would have
        -- restarted its counter, and one that had died would answer nothing.
        --
        -- The hash equality itself is pinned in C++ (`reload_tests.cpp`), where
        -- the two worlds can be compared without a tick counter that only one
        -- of the two paths restarts.
        local later = sampleAt(session, SampleTick * 3)
        assert.eq(later.tick, SampleTick * 3)
        session.close()
    end)

    suite:case("a project the size of the M2 example reloads well inside the budget", function()
        local root = temporaryProject("heavy")
        local entry = projectlib.joined(root, "src", "scripts", "main.luau")
        fs.writeStringToFile(entry, heavyScript("1"))

        local session = startSession(root)
        waitFor(session, "the engine to attach", function(message)
            return message.type == "hello"
        end)

        local spans: { number } = {}
        for round = 1, 3 do
            fs.writeStringToFile(entry, heavyScript(tostring(round + 1)))
            local reloaded = waitFor(session, `reload {round}`, function(message)
                return message.type == "reloaded"
            end)
            assert.that(reloaded.ok == true, `the reload failed: {tostring(reloaded.detail)}`)
            table.insert(spans, tonumber(reloaded.ms) or math.huge)
            table.remove(session.inbox, table.find(session.inbox, reloaded) :: number)
        end
        session.close()

        table.sort(spans)
        local worst = spans[#spans]
        print(
            `    500-instance reload: median {string.format("%.1f", spans[2])} ms, worst {string.format("%.1f", worst)} ms`
        )
        assert.that(worst < ReloadBudgetMs, `the worst reload took {worst} ms, over the {ReloadBudgetMs} ms budget`)
    end)
end)
