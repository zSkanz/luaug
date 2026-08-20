// The WebSocket client against a loopback server that sends bytes a test
// chooses. What is proved here is what no published vector can be: that the
// socket, the handshake and the reassembly loop agree with each other.

#include <doctest/doctest.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "loopback_server.h"

#include "luaug/core/i18n.h"
#include "luaug/net/websocket.h"
#include "luaug/net/websocket_protocol.h"

using luaug::core::u16;
using luaug::core::u8;
using luaug::core::usize;
using luaug::net::WebSocketClient;
using luaug::net::WebSocketClientOptions;
using luaug::net::testing::Connection;
using luaug::net::testing::LoopbackServer;
namespace ws = luaug::net::ws;

namespace
{

// M2 Finding 11: an error is identified by the `[key]` prefix, and the prefix
// is the catalog's name for the key. Without the catalog every raise reads
// `[i18n:missing:xxxxxxxx]`.
void seedRealCatalog()
{
    const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// Reads the client's upgrade request and answers it correctly. `computeAccept`
// is ours, but it is pinned to RFC 6455 §1.3's worked example by its own test,
// so using it here does not make this a self-consistency check.
std::string completeHandshake(Connection& connection)
{
    const std::string request = connection.readUntil("\r\n\r\n");

    const std::string marker = "Sec-WebSocket-Key: ";
    const usize at = request.find(marker);
    REQUIRE(at != std::string::npos);
    const usize end = request.find("\r\n", at);
    const std::string key = request.substr(at + marker.size(), end - at - marker.size());

    connection.write(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: "
        + ws::computeAccept(key) + "\r\n\r\n");

    return request;
}

// A server frame: unmasked, because §5.1 forbids a server to mask.
std::vector<u8> serverFrame(ws::Opcode opcode, bool fin, std::string_view payload)
{
    std::vector<u8> out;
    out.push_back(static_cast<u8>((fin ? 0x80u : 0x00u) | static_cast<u8>(opcode)));
    REQUIRE(payload.size() <= 125);
    out.push_back(static_cast<u8>(payload.size()));
    for (const char c : payload)
        out.push_back(static_cast<u8>(c));
    return out;
}

WebSocketClientOptions optionsFor(const LoopbackServer& server)
{
    WebSocketClientOptions options;
    options.host = "127.0.0.1";
    options.port = server.port();
    options.path = "/control";
    options.connectTimeoutMs = 5000;
    return options;
}

} // namespace

TEST_CASE("the client completes a handshake and the server sees a conforming request")
{
    LoopbackServer server;
    std::string request;
    server.serve([&request](Connection& connection) { request = completeHandshake(connection); });

    WebSocketClient client;
    const auto error = client.connect(optionsFor(server));
    server.join();

    CHECK(server.failure().empty());
    if (error.has_value())
        FAIL(error->message);
    CHECK(client.isOpen());

    CHECK(request.starts_with("GET /control HTTP/1.1\r\n"));
    CHECK(request.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos);
    CHECK(request.find("Upgrade: websocket\r\n") != std::string::npos);
}

TEST_CASE("a wrong accept value fails the connection instead of proceeding")
{
    seedRealCatalog();

    LoopbackServer server;
    server.serve(
        [](Connection& connection)
        {
            connection.readUntil("\r\n\r\n");
            connection.write(
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n");
        });

    WebSocketClient client;
    const auto error = client.connect(optionsFor(server));
    server.join();

    REQUIRE(error.has_value());
    CHECK(error->key.hash == LUAUG_TR("net.err.handshake_accept_mismatch").hash);
    CHECK_FALSE(client.isOpen());
}

TEST_CASE("what the client sends arrives as a masked text frame")
{
    LoopbackServer server;
    std::string decoded;
    bool wasMasked = false;

    server.serve(
        [&decoded, &wasMasked](Connection& connection)
        {
            completeHandshake(connection);

            // Two bytes of header, then the mask and the payload. The length is
            // read from the header rather than assumed, which is the only way
            // this reads a frame the client chose the size of.
            const std::vector<u8> header = connection.read(2);
            const usize length = static_cast<usize>(header[1] & 0x7Fu);
            wasMasked = (header[1] & 0x80u) != 0u;

            std::vector<u8> whole = header;
            const std::vector<u8> rest = connection.read((wasMasked ? 4u : 0u) + length);
            whole.insert(whole.end(), rest.begin(), rest.end());

            const ws::DecodeResult result = ws::decodeFrame(whole, 1u << 20);
            REQUIRE(result.status == ws::DecodeStatus::Ok);
            decoded.assign(result.frame.payload.begin(), result.frame.payload.end());
        });

    WebSocketClient client;
    REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());
    REQUIRE_FALSE(client.sendText(R"({"type":"hello","protocol":1})").has_value());
    server.join();

    CHECK(server.failure().empty());
    CHECK(wasMasked);
    CHECK(decoded == R"({"type":"hello","protocol":1})");
}

TEST_CASE("a fragmented message is delivered whole, once")
{
    LoopbackServer server;
    server.serve(
        [](Connection& connection)
        {
            completeHandshake(connection);
            connection.write(serverFrame(ws::Opcode::Text, false, R"({"type":)"));
            connection.write(serverFrame(ws::Opcode::Continuation, false, R"("reload",)"));
            connection.write(serverFrame(ws::Opcode::Continuation, true, R"("id":7})"));
            connection.readUntil("\x88"); // wait for the client's Close
        });

    WebSocketClient client;
    REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());

    std::string message;
    bool received = false;
    REQUIRE_FALSE(client.receiveText(message, received, 5000).has_value());
    CHECK(received);
    CHECK(message == R"({"type":"reload","id":7})");

    client.close();
    server.join();
    CHECK(server.failure().empty());
}

TEST_CASE("a ping is answered without the caller ever seeing it")
{
    LoopbackServer server;
    bool sawPong = false;

    server.serve(
        [&sawPong](Connection& connection)
        {
            completeHandshake(connection);
            connection.write(serverFrame(ws::Opcode::Ping, true, "beat"));

            const std::vector<u8> header = connection.read(2);
            sawPong = header[0] == 0x8Au && (header[1] & 0x80u) != 0u;
            connection.read(4u + static_cast<usize>(header[1] & 0x7Fu));

            connection.write(serverFrame(ws::Opcode::Text, true, "after"));
            connection.readUntil("\x88");
        });

    WebSocketClient client;
    REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());

    std::string message;
    bool received = false;
    REQUIRE_FALSE(client.receiveText(message, received, 5000).has_value());
    client.close();
    server.join();

    CHECK(server.failure().empty());
    CHECK(sawPong);
    // The ping never surfaced: the first message the caller sees is the text.
    CHECK(received);
    CHECK(message == "after");
}

TEST_CASE("a close from the server ends the connection with the closed key")
{
    seedRealCatalog();

    LoopbackServer server;
    server.serve(
        [](Connection& connection)
        {
            completeHandshake(connection);
            connection.write(serverFrame(ws::Opcode::Close, true, ""));
            // Do not read the client's reply; the point is that the client
            // stops regardless of what happens next.
        });

    WebSocketClient client;
    REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());

    std::string message;
    bool received = false;
    const auto error = client.receiveText(message, received, 5000);
    server.join();

    REQUIRE(error.has_value());
    CHECK(error->key.hash == LUAUG_TR("net.err.closed").hash);
    CHECK_FALSE(received);
    CHECK_FALSE(client.isOpen());
}

TEST_CASE("the ways a server can break the protocol all close the connection")
{
    seedRealCatalog();

    const auto rejects = [](const std::function<void(Connection&)>& misbehave)
    {
        LoopbackServer server;
        server.serve(
            [&misbehave](Connection& connection)
            {
                completeHandshake(connection);
                misbehave(connection);
            });

        WebSocketClient client;
        REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());

        std::string message;
        bool received = false;
        const auto error = client.receiveText(message, received, 5000);
        server.join();

        REQUIRE(error.has_value());
        // Each violation has its own key now; what this pins is that the
        // connection did not survive whichever one it was.
        CHECK(error->key.hash != LUAUG_TR("net.err.closed").hash);
        CHECK_FALSE(client.isOpen());
    };

    SUBCASE("it masks a frame, which section 5.1 forbids a server to do")
    {
        rejects(
            [](Connection& connection)
            {
                std::vector<u8> masked;
                ws::encodeFrame(masked, ws::Opcode::Text, true, std::span<const u8>{}, 0x11223344u);
                connection.write(masked);
            });
    }
    SUBCASE("it continues a message that was never started")
    {
        rejects([](Connection& connection) { connection.write(serverFrame(ws::Opcode::Continuation, true, "x")); });
    }
    SUBCASE("it starts a message before finishing the last one")
    {
        rejects(
            [](Connection& connection)
            {
                connection.write(serverFrame(ws::Opcode::Text, false, "a"));
                connection.write(serverFrame(ws::Opcode::Text, true, "b"));
            });
    }
    SUBCASE("it sends binary on a text-only channel")
    {
        rejects([](Connection& connection) { connection.write(serverFrame(ws::Opcode::Binary, true, "\x01\x02")); });
    }
    SUBCASE("it sets a reserved bit")
    {
        rejects(
            [](Connection& connection)
            {
                const std::vector<u8> frame{0xC1u, 0x00u};
                connection.write(frame);
            });
    }
}

TEST_CASE("a timeout is not a failure, and it does not lose the half-arrived message")
{
    LoopbackServer server;
    server.serve(
        [](Connection& connection)
        {
            completeHandshake(connection);
            // Half a frame, then a pause the client must sit through, then the
            // rest. A reader that discarded its buffer on timeout would lose it.
            const std::vector<u8> head{0x81u, 0x05u, 0x48u, 0x65u};
            connection.write(head);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const std::vector<u8> tail{0x6cu, 0x6cu, 0x6fu};
            connection.write(tail);
            connection.readUntil("\x88");
        });

    WebSocketClient client;
    REQUIRE_FALSE(client.connect(optionsFor(server)).has_value());

    std::string message;
    bool received = true;
    REQUIRE_FALSE(client.receiveText(message, received, 50).has_value());
    CHECK_FALSE(received);
    CHECK(client.isOpen());

    REQUIRE_FALSE(client.receiveText(message, received, 5000).has_value());
    CHECK(received);
    CHECK(message == "Hello");

    client.close();
    server.join();
    CHECK(server.failure().empty());
}

TEST_CASE("connecting where nothing listens fails quickly rather than stalling")
{
    seedRealCatalog();

    u16 abandonedPort = 0;
    {
        LoopbackServer server;
        abandonedPort = server.port();
    }

    WebSocketClientOptions options;
    options.host = "127.0.0.1";
    options.port = abandonedPort;
    options.connectTimeoutMs = 1500;

    const auto started = std::chrono::steady_clock::now();
    WebSocketClient client;
    const auto error = client.connect(options);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(error.has_value());
    // Any of the connect keys: which one depends on whether the OS refuses
    // immediately or lets the attempt time out, and both are the same answer
    // to the caller.
    CHECK(error->message.find("127.0.0.1") != std::string::npos);
    CHECK_FALSE(client.isOpen());

    // The OS default for this is tens of seconds. The timeout is the whole
    // reason `connect` takes one: the dev server may simply not be up yet, and
    // the engine has to be able to retry.
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 3000);
}
