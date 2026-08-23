# What a script may do

The game VM is sandboxed, always. There is no flag to turn that off and nothing
in the engine disables it to make a feature work. What a script can reach is
therefore a closed list, and this page is that list.

## Three tiers

| Tier | What lives there |
|---|---|
| **Globals** | The world model: `game`, `workspace`, `script`, `Instance`, the datatypes, `Enum`, `task`, plus Luau's own builtins. |
| **`@std/…`** | The cross-runtime standard library, shaped to match the one the Lute tooling runtime exposes. |
| **`@luaug/…`** | Optional engine libraries that are not part of the world model. |

The split is one sentence: **the world is globals, libraries are modules.** You
never require `Vector3`.

## The globals, in full

This list is exhaustive. It ends without an ellipsis on purpose: a name that is
not on it, is not a world global, and is not a datatype, **does not exist**.

**World and construction:** `game`, `workspace`, `script`, `Instance`, `Enum`.

**Datatypes:** `Vector2`, `Vector3`, `CFrame`, `Color3`, `UDim`, `UDim2`,
`Rect`, `TweenInfo`, `RaycastParams`, `Random`, `Signal`.

**Functions:** `assert`, `error`, `print`, `warn`, `pcall`, `xpcall`, `select`,
`next`, `pairs`, `ipairs`, `rawget`, `rawset`, `rawequal`, `rawlen`,
`getmetatable`, `setmetatable`, `tonumber`, `tostring`, `type`, `typeof`,
`unpack`, `require`, `gcinfo`, and the `_VERSION` string.

**Libraries:** `task`, `vector`, `buffer`, `bit32`, `math`, `table`, `string`,
`coroutine`, `utf8`, `debug`, and `os` — which carries `os.clock`, `os.time` and
`os.date` and nothing else. `os.getenv`, `os.remove` and the rest of the
process-facing surface are **absent**, not stubbed.

`warn` is the engine's; `print` is Luau's own.

## What was removed, and stays removed

`wait`, `spawn`, `delay`, `tick`, `time`, `elapsedTime`, `loadstring`,
`getfenv`, `setfenv`, `newproxy`, `shared`, and `io`.

`_G` exists and is **frozen empty**. A write to it *raises* — so it never gains
a key and is not a back channel between scripts. Shared state goes through a
module, which is a real dependency the analyzer can see.

Naming an undeclared global is itself an error under strict mode, so you find
out about a removed name at analysis time rather than at runtime. The removals
are enforced twice: by the generated type definitions, and by a test that
inspects the sandboxed global table directly.

## The modules a game VM actually has

This is short, and it is the whole list:

| Module | What it gives you |
|---|---|
| `@std/net` | `net.request` — an HTTP client that yields. |
| `@luaug/camera` | Third-person and orbit camera rigs. |
| `@luaug/testing` | The test runner the conformance suite is written against. |

Plus the `task` global, which is a global and **not** a module —
`require("@std/task")` does not resolve.

**That is the current surface, and the rest of the standard library is not in
the game VM yet.** `@std/json`, `@std/fs`, `@std/path`, `@std/stringext`,
`@std/tableext`, `@std/test` and `@std/io` are all part of the intended
cross-runtime surface and none of them is reachable from a script today. A
project that needs JSON carries its own encoder in a module; a project that
needs a filesystem does not have one.

`@std/process` and `@std/luau` are **never** going into the game VM: the first is
process control and the second is a `loadstring` equivalent. Both are tooling
only.

## There is no filesystem

A script cannot read or write a file. There is no `@std/fs` in the game VM at
all, so neither the read-only `asset://` root nor a writable `save://` root
exists yet as something Luau can reach. Content is loaded by the engine, through
the `Content` properties that name it — `MeshPart.MeshContent`, `Sound.Content`,
`ImageLabel.Image` — and that is the whole of a game's access to its own files.

Persistence today therefore means a backend. See
[Talking to a backend](manual:guides/backend).

## The network surface

`net.request` is available and it is an **HTTP client**: it takes a URL, a
method, headers, a body and a timeout in seconds, it yields the calling
coroutine, and it resumes at a frame safe point when the answer arrives.

```luau
--!strict
local net = require("@std/net")

local response = net.request({
    url = "http://127.0.0.1:8080/scores",
    method = "GET",
    timeout = 5,
})
```

Three things about it that catch people out:

- **`response.ok` is about the transport, not the status code.** A 404 is a
  server answering: `ok` is `true` and `statusCode` is 404. A refused connection
  is `ok = false`.
- **It never raises for a network condition.** A timeout, a bad host and a
  refused connection all come back as a value. It raises only for a malformed
  *call*, because that is a bug in the script.
- **`https://` is refused rather than downgraded.** This build vendors no TLS,
  and quietly falling back to plaintext would put credentials on the wire from a
  line that reads as secure.

There is no server, no raw socket and no WebSocket client for a script.
`net.serve` is a reserved name rather than a missing feature: the engine never
listens on a port in any build, which is a security property rather than an
omission.

The `[permissions]` table in `luaug.toml` parses and is reserved for the day
`serve` and a filesystem arrive; nothing reads it today.

## Why it is shaped this way

A sandbox that can be disabled is a sandbox that will be disabled, once, by
somebody in a hurry, and then depended on. The engine takes the other side of
that trade: the surface is smaller, it is written down, and the two things that
enforce it — the type definitions and a test against the live global table —
both fail loudly rather than degrading.

## Where to look next

- [Talking to a backend](manual:guides/backend) — the permitted network surface
  in practice
- [Globals](site:globals) — the reference page
