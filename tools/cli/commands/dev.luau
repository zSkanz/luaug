--!strict
--[[
	`luaug dev` — the loop this milestone exists to sell.

	Starts the dev server, launches the engine pointed at it, and turns every
	save into one world restart. The engine is a child process that dials back
	in over WebSocket (ADR 0035); `process.run` yields rather than blocking
	(U-56), so the watcher and the server keep running for as long as the engine
	does.
]]

local process = require("@std/process")
local task = require("@std/task")

local catalog = require("../catalog")
local devserver = require("../devserver")
local projectlib = require("../project")

local tr = catalog.tr

-- ADR 0024's hard requirement, and what the reload span is measured against
-- (M3 brief Decision 10). Reported rather than enforced here: the gate is what
-- fails on it, and a developer wants to be told, not stopped.
local ReloadBudgetMs = 500

export type Options = {
    path: string,
    headless: boolean,
    port: number?,
}

local function run(options: Options): number
    local project = projectlib.load(options.path)
    if project.diagnostic ~= nil then
        print(project.diagnostic)
        return 2
    end
    if not projectlib.isProject(project) then
        print(tr("cli.err.not_a_project", { path = project.root }))
        return 2
    end

    -- A project that mounts nothing would start, attach, watch, and reload an
    -- empty world forever while reporting success at every step. Refusing here
    -- is the emptiness check the brief's protocol section asks for.
    local snapshot = projectlib.sourceSnapshot(project.root)
    local hasScripts = false
    for file in snapshot do
        if string.find(file, "scripts", 1, true) ~= nil then
            hasScripts = true
            break
        end
    end
    if not hasScripts then
        print(tr("cli.dev.err.no_scripts", { path = project.root }))
        return 2
    end

    local engine = projectlib.findEngine()
    if engine == nil then
        print(tr("cli.err.no_engine"))
        return 2
    end

    local token = devserver.mintToken()
    local port = options.port or projectlib.devPort(project)

    local server: devserver.Handle
    server = devserver.start({
        root = project.root,
        port = port,
        token = token,

        onEngineAttached = function(message)
            print(tr("cli.dev.attached", { engine = tostring(message.engine) }))
        end,

        onChanged = function(changed)
            local shown: { string } = {}
            for index, file in changed do
                if index > 3 then
                    table.insert(shown, `+{#changed - 3} more`)
                    break
                end
                table.insert(shown, file)
            end
            print(tr("cli.dev.reloading", { files = table.concat(shown, ", ") }))

            server:sendToEngine({ type = "reload", id = server:nextRequestId(), paths = changed })
        end,

        onEngineMessage = function(message)
            if message.type ~= "reloaded" then
                return
            end
            if message.ok ~= true then
                print(tr("cli.dev.reload_failed", { detail = tostring(message.detail or "") }))
                return
            end

            local ms = tonumber(message.ms) or 0
            print(tr("cli.dev.reloaded", {
                ms = string.format("%.1f", ms),
                scripts = tostring(message.scripts),
                preserved = tostring(message.preserved),
            }))
            if ms > ReloadBudgetMs then
                print(tr("cli.dev.reload_slow", { ms = string.format("%.1f", ms), budget = ReloadBudgetMs }))
            end
        end,
    })

    print(tr("cli.dev.listening", { port = server.port }))

    local arguments: { string } = {
        engine :: string,
        project.root,
        `--dev-control=ws://127.0.0.1:{server.port}/control`,
        `--dev-token={token}`,
    }
    if options.headless then
        table.insert(arguments, "--headless")
    end

    -- Yields rather than blocking, so the server and the watcher above keep
    -- running for as long as this child does (U-56). It returns when the engine
    -- exits, which is what ends the session.
    local result = process.run(arguments, { stdio = "inherit" })

    print(tr("cli.dev.engine_exited", { code = result.exitcode }))
    server:close()

    -- A tick for the loop to finish closing the socket before the process ends.
    task.wait(0.05)
    print(tr("cli.dev.stopped"))

    return if result.ok then 0 else 1
end

return {
    run = run,
}
