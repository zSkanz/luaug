# 0035 — The engine is a WebSocket client of the dev server; only the dev server listens

- Status: accepted
- Date: 2026-08-20
- Supersedes: nothing. Its own first draft (stdio to the engine) was refuted
  before any code was written — see "What this ADR nearly said".

## Context
`api-design.md` §3.2 specified the hot-reload transport as the dev server
pushing `{script-changed | asset-changed | eval}` messages "to the runtime on a
localhost port", and the roadmap's M3 scope names a "WebSocket control channel".
Both leave open which side listens, and neither says why a socket rather than a
pipe between a parent process and the child it spawned.

The dev server is a Lute app (ADR 0003). What Lute 1.0.0 can actually do decides
this, and it was read from the typedefs `rokit` installed at the pin rather than
from the research report:

- **`process.run` returns after the child exits**, handing back `stdout` and
  `stderr` as whole strings. There is no child handle, no stdin, no incremental
  read (U-55). A pipe protocol to a *running* engine cannot be written.
- **There is no raw TCP.** The whole net surface is an HTTP client, a WebSocket
  client, an HTTP server and a WebSocket server (U-57).
- **`process.run` yields the calling coroutine** rather than blocking the
  runtime, so one task can run the engine to completion while another serves
  (U-56).

So the only bidirectional, server-push channel Lute can speak is WebSocket.
§3.2 was right, and for a reason it did not state.

## Decision
`luaug dev` runs an `@std/net` **WebSocket server** on the `[dev] port` and
launches `luaug-host` in a spawned task. The engine **connects out to it as a
WebSocket client** and speaks a JSON message protocol over that connection. The
same server serves the dev server's other clients — the M3 E2E gate test today,
the overlay console and any editor integration later — and relays between them
and the engine.

**Only the dev server listens.** The engine opens no port in any profile, and
the client half is compiled into dev builds only.

This costs a small `net` seam in C++: a TCP client, the HTTP upgrade handshake
with the SHA-1 + base64 `Sec-WebSocket-Accept` check, and RFC 6455 frame
read/write with client-side masking. It is tested against the worked handshake
example in RFC 6455 §1.3 and its framing examples in §5.7, which are published
vectors rather than our own output.

## Consequences
No listener on the developer's machine: no port for the engine to allocate or
collide on, no firewall prompt on first run, and no inbound parse surface
reachable by anything else on the box. The connection is outbound to a port the
dev server already owns and dies when either end goes.

The direction is the reverse of what §3.2's wording implies ("pushes … to the
runtime on a localhost port"), so §3.2 is corrected in the same commit
(MASTER_PROMPT §5). Every message type it names still exists and still arrives.

What is given up is attaching to an engine that `luaug dev` did not start; the
engine dials out, so something must be listening first. That is editor
territory, deferred with the editor by ADR 0017 and R15.

## What this ADR nearly said

The first draft, written at M3 kickoff before the typedefs were read, chose
stdio: `luaug dev` would launch the engine as a child and speak a line-delimited
JSON protocol over its stdin/stdout, keeping the WebSocket entirely inside the
dev server and the engine free of network code. The reasoning was sound and the
premise was false — Lute 1.0.0 cannot hold a pipe to a running child (U-55).

Recorded rather than deleted, because the next reader will have the same idea
for the same good reasons, and the refutation is one typedef file away.
