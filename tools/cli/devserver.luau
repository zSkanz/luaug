--!strict
--[[
	The dev server: the WebSocket the engine dials into, the watcher that
	notices a save, and the relay between the two (ADR 0035).

	**Only this listens.** The engine opens no port in any profile; it connects
	out to the port this binds, proves it is the process we launched with the
	token on its command line, and receives everything over that one connection.
	Every other client -- the M3 gate's end-to-end test today, the overlay
	console later -- is an ordinary WebSocket client of the same server and gets
	the engine's replies relayed verbatim.

	The watcher does not trust its own events. `fs.watch` reports the top-level
	entry rather than the changed file, fires two to six times per save, and on
	inotify may not see into a subdirectory at all (U-58) -- so every directory
	is watched individually, an event only marks the tree dirty, and a quiet
	period later the source set is rescanned and diffed. What reaches the engine
	is the real changed-file list.
]]

local fsRaw = require("@lute/fs")
local json = require("@std/json")
local net = require("@lute/net")
local task = require("@lute/task")
local time = require("@lute/time")

local project = require("./project")

export type Message = { [string]: any }

export type Options = {
    root: string,
    port: number,
    token: string,
    -- Seconds of quiet before a save is acted on. Two to six events per save
    -- would otherwise be two to six world restarts.
    debounce: number?,
    onEngineMessage: ((Message) -> ())?,
    onEngineAttached: ((Message) -> ())?,
    onChanged: (({ string }) -> ())?,
    onLog: ((string) -> ())?,
}

export type Handle = {
    port: number,
    engineAttached: (Handle) -> boolean,
    sendToEngine: (Handle, Message) -> boolean,
    nextRequestId: (Handle) -> number,
    close: (Handle) -> (),
}

local DefaultDebounce = 0.05

--- Not a secret in the cryptographic sense and not pretending to be one. What
--- it buys is that another process on this machine cannot drive your engine by
--- guessing a port, which is the whole exposure a loopback listener has.
local function mintToken(): string
    local stamp = string.format("%.9f", time.now() :: any)
    return (string.gsub(`{stamp}-{math.random(1, 2 ^ 30)}`, "[^%w]", ""))
end

local function start(options: Options): Handle
    local debounce = options.debounce or DefaultDebounce
    local log = options.onLog or function(_line: string) end

    local engineSocket: net.ServerWebSocket? = nil
    local observers: { net.ServerWebSocket } = {}
    local nextId = 1

    local function relayToObservers(text: string)
        for _, socket in observers do
            socket:send(text)
        end
    end

    local function handleFromEngine(text: string)
        relayToObservers(text)

        local decoded = json.deserialize(text)
        local message = json.asObject(decoded)
        if message == nil then
            return
        end

        if message.type == "hello" then
            if options.onEngineAttached ~= nil then
                options.onEngineAttached(message :: any)
            end
            return
        end
        if options.onEngineMessage ~= nil then
            options.onEngineMessage(message :: any)
        end
    end

    local server = net.server.serve({
        hostname = "127.0.0.1",
        port = options.port,
        handler = function(_request, _server)
            -- The HTTP side exists only so the upgrade has something to answer
            -- from. There is no page here and there is not meant to be.
            return { status = 426, body = "" }
        end,
        websocket = {
            message = function(socket, raw)
                local text = if typeof(raw) == "string" then raw else buffer.tostring(raw :: buffer)

                if socket == engineSocket then
                    handleFromEngine(text)
                    return
                end

                local decoded = json.deserialize(text)
                local message = json.asObject(decoded)

                -- The engine identifies itself by carrying the token this
                -- process minted and passed on its command line. Anything else
                -- is an observer: it may listen, and it may ask the engine for
                -- things, but it cannot claim to *be* the engine.
                if
                    engineSocket == nil
                    and message ~= nil
                    and message.type == "hello"
                    and message.role == "engine"
                    and message.token == options.token
                then
                    engineSocket = socket
                    handleFromEngine(text)
                    return
                end

                if engineSocket ~= nil and message ~= nil then
                    (engineSocket :: net.ServerWebSocket):send(text)
                end
            end,
            close = function(socket, _code, _message)
                if socket == engineSocket then
                    engineSocket = nil
                    return
                end
                for index, observer in observers do
                    if observer == socket then
                        table.remove(observers, index)
                        break
                    end
                end
            end,
            open = function(socket)
                -- Everyone starts as an observer. The engine promotes itself by
                -- sending a `hello` carrying the token.
                table.insert(observers, socket)
            end,
        },
    })

    -- --- the watcher ---------------------------------------------------------

    local snapshot = project.sourceSnapshot(options.root)
    local dirty = false
    local watched: { fsRaw.WatchHandle } = {}
    local running = true

    for _, dir in project.sourceDirectories(options.root) do
        table.insert(
            watched,
            fsRaw.watch(dir, function(_filename, _event)
                -- The event says only that something under here moved. What
                -- changed is decided by the rescan below (U-58).
                dirty = true
            end)
        )
    end
    log(`watching {#watched} directories`)

    task.spawn(function()
        while running do
            task.wait(debounce)
            if not dirty then
                continue
            end
            dirty = false

            local fresh = project.sourceSnapshot(options.root)
            local changed = project.changedFiles(snapshot, fresh)
            snapshot = fresh
            if #changed == 0 then
                -- An editor touched a file without changing it, or wrote a
                -- temporary beside it. Nothing to reload.
                continue
            end

            if options.onChanged ~= nil then
                options.onChanged(changed)
            end
        end
    end)

    local handle = {} :: Handle
    handle.port = server.port

    function handle.engineAttached(_self: Handle): boolean
        return engineSocket ~= nil
    end

    function handle.nextRequestId(_self: Handle): number
        nextId += 1
        return nextId
    end

    function handle.sendToEngine(_self: Handle, message: Message): boolean
        local socket = engineSocket
        if socket == nil then
            return false
        end
        socket:send(json.serialize(json.object(message :: any)))
        return true
    end

    function handle.close(_self: Handle)
        running = false
        for _, watcher in watched do
            watcher:close()
        end
        server.close()
    end

    return handle
end

return {
    start = start,
    mintToken = mintToken,
}
