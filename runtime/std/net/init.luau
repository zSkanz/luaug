--!strict
--[[
	`@std/net` — the TYPE side of a module whose body is C++.

	**This file is never executed by the engine.** `scriptRequire` matches an
	engine-provided module before it ever reaches the loader (`modules.cpp`), and
	`@std/net` is registered natively, so what runs is `engine/script/src/net_module.cpp`.
	What this file is for is `luau-analyze`: the `.luaurc` alias points `@std` at
	this directory, so this is where a strict-mode script learns the shape.

	Which means the two CAN drift, and the guard against that is the conformance
	suite rather than good intentions: `tests/conformance/std/net.spec.luau`
	asserts the surface from inside the running VM, so a field that exists here
	and not there fails a gate rather than a script.

	The `error` below is the other half. If the engine ever stops matching this
	name natively, a script gets a loud message naming the cause instead of a
	table of nils.

	Naming is Lute's, not ADR 0034's, and deliberately: `@std/*` exists to be the
	Lute-compatible surface (ADR 0030) so that backend and utility code runs
	unchanged on both runtimes. See the header of `net_module.cpp`.
]]

--- What `net.request` accepts.
---
--- `timeout` is in SECONDS, like every other duration a script writes in this
--- engine (`task.wait`, `TweenInfo`).
export type RequestOptions = {
    --- `http://` only in v1. An `https://` URL is REFUSED rather than downgraded
    --- to plaintext: this build vendors no TLS library, and a silent downgrade
    --- would put credentials on the wire from a line that reads as secure.
    url: string,
    method: string?,
    headers: { [string]: string }?,
    body: string?,
    timeout: number?,
}

--- What `net.request` returns.
---
--- `ok` is about the TRANSPORT and not about the status code: a 404 is a server
--- answering, so it arrives with `ok = true` and `statusCode = 404`. A call that
--- never reached a server has `ok = false` and the reason in `statusMessage`.
export type Response = {
    ok: boolean,
    statusCode: number,
    statusMessage: string,
    headers: { [string]: string },
    body: string,
}

export type Net = {
    --- Performs an HTTP/1.1 request. **Yields** — the calling coroutine is
    --- parked and resumed at a frame safe point when the answer arrives, so a
    --- slow server costs the caller time and costs the frame nothing.
    ---
    --- Never raises for a network condition: a refused connection, a bad URL and
    --- a timeout all come back as `ok = false`. It raises only for a malformed
    --- CALL — a missing `url`, a field of the wrong type — because that is a bug
    --- in the script rather than a fact about the world.
    request: (options: RequestOptions) -> Response,
}

return (error("@std/net is provided by the engine; this file is the type declaration and is never run") :: any) :: Net
