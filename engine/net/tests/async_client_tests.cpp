// The async HTTP client, and the two defects that shaped it.
//
// Both were found by `@std/net`'s first request never coming back to Luau, and
// both are the kind that a smaller test would never produce: one needs a caller
// polling in a tight loop, the other needs a port with nothing behind it.
#include "luaug/core/i18n.h"
#include "luaug/net/async_client.h"
#include "luaug/net/tcp.h"

#include <chrono>
#include <doctest/doctest.h>
#include <string>
#include <thread>

#include "loopback_server.h"

using namespace luaug;
using namespace luaug::net;
using luaug::core::f64;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// Nothing is listening here, so a connection is refused. Which is the point:
// "refused" is the fastest possible network answer, and anything slow about it
// is ours.
constexpr u16 DeadPort = 47189;

// How long the slow-server handler takes to answer. At namespace scope because
// a lambda that names a function-local constant odr-uses it, and Clang wants it
// captured where MSVC does not.
constexpr int SlowReplyMs = 400;

[[nodiscard]] f64 millisecondsSince(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<f64, std::milli>(elapsed).count();
}

} // namespace

TEST_CASE("a refused connection fails fast rather than waiting out the timeout")
{
    seedCatalog();

    // D035. On Windows a FAILED non-blocking connect is reported in `exceptfds`
    // and never in `writefds`, so a `select` watching only writability cannot
    // see a refusal: it waits the whole timeout and then reports one. The
    // symptom was ten seconds to discover that nothing was listening on a
    // loopback port that had answered instantly.
    //
    // A LONG timeout against a loose bound, and both halves matter. What is
    // being pinned is "does not wait out the timeout" and nothing narrower: how
    // fast a refusal actually arrives is the OS's business -- on the machine
    // this was written on it is about two seconds, because the local firewall
    // drops the SYN rather than answering it, and elsewhere it is microseconds.
    // With the bug the answer is always exactly `timeoutMs`, so a twenty-second
    // timeout and an eight-second bound separate the two on any machine without
    // pinning a latency nobody controls.
    constexpr u32 timeoutMs = 20000;
    const auto started = std::chrono::steady_clock::now();

    TcpStream stream;
    const auto error = stream.connect("127.0.0.1", DeadPort, timeoutMs);
    const f64 elapsed = millisecondsSince(started);

    REQUIRE(error.has_value());
    CHECK(elapsed < 8000.0);
}

TEST_CASE("a request completes even while a caller polls in a tight loop")
{
    seedCatalog();

    // D036, and it is a fairness bug rather than a logic one. The frame loop
    // calls `take` once per frame for every outstanding ticket; a headless run
    // on the null backend has no frame pacing at all, so that is a hundred
    // thousand lock/unlock pairs on the same mutex the worker needs for the one
    // instant it takes to file a result. `std::mutex` is not fair, the worker
    // never got in, and a request that had completed on the socket never came
    // back to Luau.
    //
    // The loop below is that caller. Without the lock-free early out in `take`
    // this does not finish.
    AsyncClient client;

    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(DeadPort) + "/";
    request.timeoutMs = 3000;

    const NetTicket ticket = client.submit(request);
    REQUIRE(ticket.valid());

    NetResult result;
    bool taken = false;
    const auto started = std::chrono::steady_clock::now();
    while (!taken && millisecondsSince(started) < 10'000.0) {
        taken = client.take(ticket, result);
    }

    REQUIRE(taken);
    // A refused connection is an ERROR from the client's point of view: nothing
    // answered, so there is no response to report.
    CHECK(result.error.has_value());
    CHECK(client.outstanding() == 0);
}

TEST_CASE("a completed request is taken exactly once")
{
    seedCatalog();

    AsyncClient client;
    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(DeadPort) + "/";
    request.timeoutMs = 3000;

    const NetTicket ticket = client.submit(request);
    NetResult result;
    while (!client.take(ticket, result)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The second take is false, not a duplicate. A resumed coroutine must not be
    // resumed again by the next frame's pump.
    CHECK_FALSE(client.take(ticket, result));
    CHECK_FALSE(client.take(NetTicket{999}, result));
}

TEST_CASE("two requests are in flight at once rather than one after the other")
{
    seedCatalog();

    // Two workers is the default, and this is why it is not one: a script that
    // fires two requests at a page load should not have the second wait for the
    // first. Both go to a server that answers slowly, so serial execution would
    // take twice as long as parallel -- measured as a ratio rather than against
    // a wall-clock budget, because only the ratio is a property of the client.
    testing::LoopbackServer first;
    testing::LoopbackServer second;
    const auto slowHandler = [](testing::Connection& connection) {
        (void)connection.readUntil("\r\n\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(SlowReplyMs));
        connection.write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nslow");
        connection.close();
    };
    first.serve(slowHandler);
    second.serve(slowHandler);

    AsyncClient client;
    HttpRequest a;
    a.url = "http://127.0.0.1:" + std::to_string(first.port()) + "/";
    HttpRequest b;
    b.url = "http://127.0.0.1:" + std::to_string(second.port()) + "/";

    const auto started = std::chrono::steady_clock::now();
    const NetTicket ticketA = client.submit(a);
    const NetTicket ticketB = client.submit(b);

    NetResult resultA;
    NetResult resultB;
    bool haveA = false;
    bool haveB = false;
    while ((!haveA || !haveB) && millisecondsSince(started) < 10'000.0) {
        haveA = haveA || client.take(ticketA, resultA);
        haveB = haveB || client.take(ticketB, resultB);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const f64 elapsed = millisecondsSince(started);

    first.join();
    second.join();

    REQUIRE(haveA);
    REQUIRE(haveB);
    CHECK(resultA.response.body == "slow");
    CHECK(resultB.response.body == "slow");
    // Well under two delays. Serial would be at least that.
    CHECK(elapsed < static_cast<f64>(SlowReplyMs) * 1.8);
}

TEST_CASE("shutdown drops results nobody will ever take")
{
    seedCatalog();

    AsyncClient client;
    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(DeadPort) + "/";
    request.timeoutMs = 3000;
    const NetTicket ticket = client.submit(request);

    client.shutdown();
    CHECK(client.outstanding() == 0);

    NetResult result;
    CHECK_FALSE(client.take(ticket, result));

    // A submit after shutdown is refused rather than queued for workers that no
    // longer exist -- an invalid ticket, which is what the binding raises on.
    CHECK_FALSE(client.submit(request).valid());

    // Idempotent: the destructor calls it again.
    client.shutdown();
}
