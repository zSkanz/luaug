// A blocking HTTP/1.1 client (ADR 0012, api-design.md §7 `@std/net.request`).
//
// **Plain `http://` only, and that is a v1 limitation rather than an oversight.**
// TLS needs a TLS library; this repository vendors none, and R5 forbids adding
// one without a human-approved ADR. So an `https://` URL is REFUSED with a named
// error rather than silently downgraded to plaintext -- a downgrade would send a
// caller's credentials in the clear on a line of code that looks secure, which is
// the worst of the three available behaviours.
//
// The practical consequence, stated where a caller reads it: `net.request` in v1
// reaches a backend on localhost or on a LAN, which is the shape ADR 0012 says
// v1 is for (a sibling backend process running on Lute). It does not reach the
// public internet.
//
// HTTP/1.1 with `Connection: close` and no keep-alive, no redirects followed, no
// chunked REQUEST bodies. Chunked responses ARE decoded, because a server
// chooses that and a client cannot refuse it.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <optional>
#include <string>
#include <vector>

namespace luaug::net {

using core::u16;
using core::u32;

struct HttpHeader
{
    std::string name;
    std::string value;
};

struct HttpRequest
{
    std::string url;
    std::string method = "GET";
    std::vector<HttpHeader> headers;
    std::string body;

    // Whole-request, not per-read. A server that sends a byte a second would
    // otherwise hold a coroutine forever without ever timing out.
    u32 timeoutMs = 10'000;

    // A response larger than this is an error rather than a truncation. The
    // engine has no streaming response type in v1, so the whole body lands in
    // memory and an unbounded one is a script handing a stranger the process.
    core::usize maxBodyBytes = 8u * 1024u * 1024u;
};

struct HttpResponse
{
    // The transport succeeded and a response was parsed. Says nothing about the
    // status code: a 404 is `ok` with `statusCode == 404`, which is Lute's
    // shape and the right one -- "the server answered" and "the server agreed"
    // are different questions.
    bool ok = false;
    u16 statusCode = 0;
    std::string statusMessage;
    std::vector<HttpHeader> headers;
    std::string body;
};

// Blocking. Runs on whatever thread calls it, which is never the VM's thread --
// `@std/net` parks the coroutine and hands this to a worker (`async_net.h`).
[[nodiscard]] std::optional<core::EngineError> performHttp(const HttpRequest& request, HttpResponse& response);

// Split out so the parsing is testable without a socket, which is what lets the
// awkward cases -- a chunked body, a header with no space after the colon, a
// status line with no reason phrase -- be tested at all.
struct ParsedUrl
{
    std::string scheme;
    std::string host;
    u16 port = 80;
    // Path plus query, ready to go on the request line. Never empty: a URL with
    // no path is `/`.
    std::string target;
};

[[nodiscard]] std::optional<core::EngineError> parseUrl(std::string_view url, ParsedUrl& out);

// Parses a complete response, headers and body. `raw` must hold the entire
// response; this does no reading of its own.
[[nodiscard]] std::optional<core::EngineError> parseHttpResponse(std::string_view raw, core::usize maxBodyBytes,
                                                                 HttpResponse& out);

} // namespace luaug::net
