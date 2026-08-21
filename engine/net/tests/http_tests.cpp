// The HTTP client, tested where it can be tested without a server.
//
// `parseUrl` and `parseHttpResponse` are split out of `performHttp` for exactly
// this: the awkward cases -- a chunked body, a status line with no reason
// phrase, a header with no space after its colon -- are the ones a live-server
// test never produces on purpose, and they are where a hand-written parser goes
// wrong. The one live case at the bottom uses the loopback server the WebSocket
// tests already carry.
#include "luaug/core/i18n.h"
#include "luaug/net/http.h"

#include <doctest/doctest.h>
#include <string>

#include "loopback_server.h"

using namespace luaug;
using namespace luaug::net;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] const HttpHeader* header(const HttpResponse& response, std::string_view name)
{
    for (const HttpHeader& entry : response.headers) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a URL is split into the parts a request line needs")
{
    seedCatalog();

    ParsedUrl url;
    REQUIRE_FALSE(parseUrl("http://127.0.0.1:8080/health?deep=1", url).has_value());
    CHECK(url.scheme == "http");
    CHECK(url.host == "127.0.0.1");
    CHECK(url.port == 8080);
    CHECK(url.target == "/health?deep=1");

    // No path means `/`, not empty. An empty request target is not a request.
    REQUIRE_FALSE(parseUrl("http://example.test", url).has_value());
    CHECK(url.port == 80);
    CHECK(url.target == "/");
}

TEST_CASE("https is refused rather than quietly downgraded")
{
    seedCatalog();

    // The whole point of the test: a downgrade would put a caller's credentials
    // on the wire in the clear from a line of code that reads as secure.
    ParsedUrl url;
    const auto error = parseUrl("https://example.test/", url);
    REQUIRE(error.has_value());
    CHECK(error->message.find("net.err.http_tls_unsupported") != std::string::npos);
}

TEST_CASE("a URL that carries a credential is refused")
{
    seedCatalog();

    ParsedUrl url;
    const auto error = parseUrl("http://user:secret@example.test/", url);
    REQUIRE(error.has_value());
    CHECK(error->message.find("net.err.http_userinfo_unsupported") != std::string::npos);
}

TEST_CASE("a malformed URL is an error and not a guess")
{
    seedCatalog();

    ParsedUrl url;
    CHECK(parseUrl("example.test/thing", url).has_value());
    CHECK(parseUrl("http://", url).has_value());
    CHECK(parseUrl("http://host:0/", url).has_value());
    CHECK(parseUrl("http://host:99999/", url).has_value());
    CHECK(parseUrl("ftp://host/", url).has_value());
}

TEST_CASE("a response is parsed into status, headers and body")
{
    seedCatalog();

    HttpResponse response;
    REQUIRE_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\n"
                                    "Content-Type: application/json\r\n"
                                    "Content-Length: 13\r\n"
                                    "\r\n"
                                    "{\"ok\":true}\r\n",
                                    1024, response)
                      .has_value());
    CHECK(response.ok);
    CHECK(response.statusCode == 200);
    CHECK(response.statusMessage == "OK");
    REQUIRE(header(response, "Content-Type") != nullptr);
    CHECK(header(response, "Content-Type")->value == "application/json");
    CHECK(response.body == "{\"ok\":true}\r\n");
}

TEST_CASE("a 404 is a successful request, not a failed one")
{
    seedCatalog();

    // Lute's shape and the right one: "the server answered" and "the server
    // agreed" are different questions, and collapsing them makes every caller
    // unable to tell a missing resource from a dead host.
    HttpResponse response;
    REQUIRE_FALSE(parseHttpResponse("HTTP/1.1 404 Not Found\r\n\r\n", 1024, response).has_value());
    CHECK(response.ok);
    CHECK(response.statusCode == 404);
}

TEST_CASE("a status line with no reason phrase parses")
{
    seedCatalog();

    // Optional in the RFC, omitted by real servers, and rejected by every
    // parser written against the happy path alone.
    HttpResponse response;
    REQUIRE_FALSE(parseHttpResponse("HTTP/1.1 204\r\n\r\n", 1024, response).has_value());
    CHECK(response.statusCode == 204);
    CHECK(response.statusMessage.empty());
}

TEST_CASE("a chunked body is decoded")
{
    seedCatalog();

    HttpResponse response;
    REQUIRE_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\n"
                                    "Transfer-Encoding: chunked\r\n"
                                    "\r\n"
                                    "5\r\nhello\r\n"
                                    "7\r\n, world\r\n"
                                    "0\r\n\r\n",
                                    1024, response)
                      .has_value());
    CHECK(response.body == "hello, world");
}

TEST_CASE("a chunk extension is ignored rather than parsed as a size")
{
    seedCatalog();

    HttpResponse response;
    REQUIRE_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\n"
                                    "Transfer-Encoding: chunked\r\n"
                                    "\r\n"
                                    "5;name=value\r\nhello\r\n"
                                    "0\r\n\r\n",
                                    1024, response)
                      .has_value());
    CHECK(response.body == "hello");
}

TEST_CASE("a truncated response is a structured error and not a partial body")
{
    seedCatalog();

    HttpResponse response;
    CHECK(parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n", 1024, response).has_value());
    CHECK(parseHttpResponse("not http at all", 1024, response).has_value());
    CHECK(parseHttpResponse("HTTP/1.1 OK\r\n\r\n", 1024, response).has_value());

    // A chunked body that stops mid-chunk. The dangerous shape: a lenient
    // parser hands the caller half a document that looks whole.
    CHECK(parseHttpResponse("HTTP/1.1 200 OK\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "\r\n"
                            "20\r\nshort\r\n",
                            1024, response)
              .has_value());
}

TEST_CASE("a body larger than the limit is refused rather than truncated")
{
    seedCatalog();

    HttpResponse response;
    const auto error = parseHttpResponse("HTTP/1.1 200 OK\r\n\r\n0123456789", 4, response);
    REQUIRE(error.has_value());
    CHECK(error->message.find("net.err.http_body_too_large") != std::string::npos);
}

TEST_CASE("a real request over the loopback gets a real response")
{
    seedCatalog();

    // The one live case. It proves what the parser tests cannot: that the
    // request we write is one a server accepts, that `Connection: close` is
    // what ends the read, and that the two halves are joined the right way
    // round.
    testing::LoopbackServer server;
    server.serve([](testing::Connection& connection) {
        // Read to the header terminator, then answer. The point is not the
        // request's contents but that one ARRIVED and was well formed enough to
        // have a terminator to find.
        const std::string head = connection.readUntil("\r\n\r\n");
        if (head.rfind("GET /ping HTTP/1.1\r\n", 0) != 0) {
            throw std::runtime_error("the request line is not the one that was asked for: " + head);
        }
        if (head.find("Host: 127.0.0.1:") == std::string::npos) {
            throw std::runtime_error("no Host header derived from the URL: " + head);
        }
        if (head.find("Connection: close\r\n") == std::string::npos) {
            throw std::runtime_error("no Connection: close, so the body has no end: " + head);
        }

        connection.write("HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "pong");
        connection.close();
    });

    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/ping";
    request.timeoutMs = 5000;

    HttpResponse response;
    const auto error = performHttp(request, response);
    server.join();
    // The handler's assertions come back through here rather than through
    // doctest: an exception thrown on the server thread would terminate the
    // process instead of failing the test.
    CHECK(server.failure().empty());

    REQUIRE_FALSE(error.has_value());
    CHECK(response.ok);
    CHECK(response.statusCode == 200);
    CHECK(response.body == "pong");
}
