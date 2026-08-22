--!strict
--- `@luaug/signal` — a typed Signal for a game's own events (api-design.md §1.1).
---
--- The engine's own signals are `Signal<T...>` values on Instances and there is
--- no `BindableEvent`: an Instance was the wrong shape for a typed callback
--- list, and divergence 5 says so. This is what a game uses instead when it
--- wants an event of its own.
---
--- **It defers, exactly like the engine's own signals do (R8, ADR 0015).** That
--- is the decision inside this file and it is not a detail: a game whose own
--- events fired synchronously while every engine event fired at the next drain
--- would have two event semantics in one script, and the re-entrancy bugs that
--- come from that are the whole reason ADR 0015 refuses immediate mode. `Fire`
--- returns immediately and every handler runs at the next drain point.
---
--- **Disconnecting during a drain is safe**, and it has to be: a handler that
--- disconnects itself is the most common thing a one-shot listener does. The
--- snapshot below is what makes that true -- a drain iterates the connections
--- that existed when the fire happened, and a connection closed in the meantime
--- is skipped rather than called.
---
--- Casing follows ADR 0034: this is a module, so `signal.new` is camelCase,
--- while `Signal` and `Connection` are objects and their methods are PascalCase.

local task = require("@std/task")

local Signal = {}

export type Connection = {
    --- False once `Disconnect` has run. Read rather than a method, because it is
    --- a fact about the connection rather than a question to ask it.
    Connected: boolean,
    Disconnect: (self: Connection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, handler: (T...) -> ()) -> Connection,
    Once: (self: Signal<T...>, handler: (T...) -> ()) -> Connection,
    Wait: (self: Signal<T...>) -> T...,
    Fire: (self: Signal<T...>, T...) -> (),
    DisconnectAll: (self: Signal<T...>) -> (),
}

type Entry = {
    handler: (...any) -> (),
    connection: Connection,
}

type Internal = {
    entries: { Entry },
    Connect: (self: any, handler: (...any) -> ()) -> Connection,
    Once: (self: any, handler: (...any) -> ()) -> Connection,
    Wait: (self: any) -> ...any,
    Fire: (self: any, ...any) -> (),
    DisconnectAll: (self: any) -> (),
}

local SignalImpl = {}
SignalImpl.__index = SignalImpl

function SignalImpl.Connect(self: Internal, handler: (...any) -> ()): Connection
    local entry: Entry
    local connection: Connection = {
        Connected = true,
        Disconnect = function(this: Connection)
            if not this.Connected then
                return
            end
            this.Connected = false
            local index = table.find(self.entries, entry)
            if index ~= nil then
                table.remove(self.entries, index)
            end
        end,
    }
    entry = { handler = handler, connection = connection }
    table.insert(self.entries, entry)
    return connection
end

function SignalImpl.Once(self: Internal, handler: (...any) -> ()): Connection
    local connection: Connection
    connection = self:Connect(function(...)
        connection:Disconnect()
        handler(...)
    end)
    return connection
end

--- Yields until the next fire and returns what it carried.
---
--- The waiting thread is resumed through `task.spawn` from inside the deferred
--- drain rather than resumed directly, so a `Wait` that returns into a long
--- computation cannot stall the fire that woke it.
function SignalImpl.Wait(self: Internal): ...any
    local thread = coroutine.running()
    local connection: Connection
    connection = self:Connect(function(...)
        connection:Disconnect()
        task.spawn(thread, ...)
    end)
    return coroutine.yield()
end

function SignalImpl.Fire(self: Internal, ...: any)
    -- **A snapshot, and this is the line that makes disconnection safe.**
    -- Handlers run later, and by then the list may have changed: a handler that
    -- disconnected itself, or one that connected another. The drain runs what
    -- was connected at the moment of the fire, and checks each connection is
    -- still live before calling it.
    local snapshot = table.clone(self.entries)
    local arguments = table.pack(...)

    task.defer(function()
        for _, entry in snapshot do
            if entry.connection.Connected then
                entry.handler(table.unpack(arguments, 1, arguments.n))
            end
        end
    end)
end

function SignalImpl.DisconnectAll(self: Internal)
    -- Marked dead first, then dropped: a fire already in flight holds a
    -- snapshot, and `Connected` is what tells it those entries are gone.
    for _, entry in self.entries do
        entry.connection.Connected = false
    end
    table.clear(self.entries)
end

--- A new signal. `signal.new()` with no type argument is `Signal<...any>`;
--- annotate the variable to type its handlers:
---
--- ```lua
--- local died: Signal.Signal<string, number> = Signal.new()
--- died:Connect(function(who: string, at: number) end)
--- ```
function Signal.new<T...>(): Signal<T...>
    local instance: Internal = setmetatable({ entries = {} }, SignalImpl) :: any
    return (instance :: any) :: Signal<T...>
end

return Signal
