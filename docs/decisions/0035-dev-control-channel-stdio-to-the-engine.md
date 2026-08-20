# 0035 — The dev control channel is stdio to the engine; the WebSocket stays in the dev server

- Status: accepted
- Date: 2026-08-20

## Context
`api-design.md` §3.2 specified the hot-reload transport as the dev server
pushing `{script-changed | asset-changed | eval}` messages "to the runtime on a
localhost port", and the roadmap's M3 scope names a "WebSocket control channel".
Read literally, that puts an RFC 6455 implementation inside the engine: a TCP
listener, the SHA-1 + base64 handshake, frame parsing, a background thread, and
a socket on the developer's machine that any local process can connect to. The
engine has no `net` module — `net_api` and socket primitives are M7 scope — and
nothing WebSocket-capable is vendored, so this would be hand-written C++ whose
only consumer is a tool that already sits on the other end of a pipe.

The dev server is a Lute app (ADR 0003) and Lute has both a WebSocket server and
client. `luaug dev` starts the engine itself, so a parent/child pipe already
exists before any socket is opened.

## Decision
`luaug dev` launches `luaug-host` as a **child process** and speaks a
line-delimited JSON control protocol over its stdin/stdout. The **WebSocket
stays in the dev server**, which serves it to clients — the M3 E2E gate test
today, the overlay console and any editor integration later — and relays in both
directions. Every message type §3.2 names still exists and still reaches the
engine; only the last hop changes. The engine never opens a socket, in any
profile.

The roadmap's gate ("WebSocket confirms reload") is met literally: the test is a
WebSocket client of the dev server, and what it receives is the engine's own
reload-complete record relayed verbatim.

## Consequences
No network listener in a dev build: no port to allocate or collide on, no
firewall prompt on first run, no parse surface reachable from off-box, and no
socket lifetime to get wrong on three platforms. The channel dies with the
process that owns it. `@std/net` stays exactly where ADR 0003 put it.

What is given up is attaching to a running engine that `luaug dev` did not
start. That is editor territory, deferred with the editor by ADR 0017 and R15;
if it is ever wanted, the dev server is where the attach point belongs, because
by then it is the thing that knows how to speak to an engine.

`api-design.md` §3.2's transport bullet is corrected in the commit that
introduces this ADR (MASTER_PROMPT §5: docs follow reality).
