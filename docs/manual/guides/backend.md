# Talking to a backend

There is no data store, no remote event and no replication in this release. A
game that needs to persist something, authenticate somebody or share state
between players talks to **a server you write**, over HTTP.

That is a smaller promise than a hosted platform makes, and it is a portable
one: the backend is yours, in any language, and it outlives this engine.

## The client

```luau
--!strict
local net = require("@std/net")

local response = net.request({
    url = "http://127.0.0.1:8080/scores",
    method = "POST",
    headers = { ["Content-Type"] = "application/json" },
    body = body,
    timeout = 5,          -- SECONDS, like every other duration here
})
```

`net.request` **yields**. The calling coroutine parks and resumes at a frame
safe point when the answer arrives, so a slow server costs the caller time and
costs the frame nothing.

## Three things that catch people out

**`ok` is about the transport, not the status code.**

```luau
if not response.ok then
    -- never reached a server: refused, timed out, bad host
    warn(response.statusMessage)
elseif response.statusCode >= 400 then
    -- a server answered, and said no
end
```

A 404 is a server answering: `ok` is `true` and `statusCode` is 404.

**It does not raise for a network condition.** A refused connection, a bad URL
and a timeout all come back as a value. It raises only for a malformed *call* —
a missing `url`, a field of the wrong type — because that is a bug in the script
rather than a fact about the world.

**`https://` is refused rather than downgraded.** This build vendors no TLS
library, and a silent fallback to plaintext would put credentials on the wire
from a line that reads as secure. For anything real, terminate TLS in front of
your backend and reach it over a private network, or wait for the engine to
carry TLS.

## Naming

`net.request` returns `statusCode`, not `StatusCode`. The `@std` namespace
exists to be the same surface the tooling runtime exposes, so utility code can
run in both — and a divergence in casing would make the portability the
namespace is *for* into a lie.

Everything else in this engine follows the object/namespace rule; this one
namespace follows its own upstream.

## Where it belongs in a frame

At the **start and the end** of a session, not inside the tick. A tick is 60 Hz
and a database is not.

```luau
--!strict
local RunService = game:GetService("RunService")

-- Right: once, at boot.
task.spawn(function()
    local profile = fetchProfile()
    applyProfile(profile)
end)

-- Wrong: a request per tick.
RunService.Heartbeat:Connect(function()
    postPosition(character.Position)
end)
```

Batch, debounce, and send on a timer — or on a meaningful event, which is
usually better.

## Determinism

A response arrives when a server answers, which is a wall-clock fact. Nothing in
the network module reaches simulation state on its own, but **a script that
writes a response into the world has taken its replay's determinism into its own
hands**.

That is stated rather than prevented, because the alternative is a network API
no backend-talking game could use. Where it matters, apply the response at a
known tick rather than the moment it lands.

## What is not here

- **No server.** `net.serve` is a reserved name; the engine never listens on a
  port in any build.
- **No raw sockets, and no WebSocket client for a script.**
- **No replication, no remote events, no authority model.** A second player is
  not something this release has an opinion about.
- **No filesystem**, so no local save file. Persistence is the backend.

The `[permissions]` table in `luaug.toml` parses and is reserved against the day
`serve` and a filesystem arrive; nothing reads it today.

## Sharing code with your backend

The `@std` surface is deliberately the one the Lute tooling runtime exposes, so
a pure module — a scoring rule, a validation, a data shape — can be required by
both your game and a Lute backend without a copy.

That is the reason the namespace exists, and it is what makes "write your own
backend" less work than it sounds.

## Where to look next

- [What a script may do](manual:concepts/sandbox)
- [Shipping a game](manual:guides/shipping) — and why your source ships with it
