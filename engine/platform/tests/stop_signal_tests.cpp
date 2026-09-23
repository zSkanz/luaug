// Asking a running engine to stop from outside it (`stop_signal.h`).
#include "luaug/platform/stop_signal.h"

#include <csignal>
#include <doctest/doctest.h>

using namespace luaug;

TEST_CASE("an interrupt asks the engine to stop rather than killing it")
{
    // **The first request only asks**: the process is still here to run the
    // game's close handlers. The second would end it, so a test raises one.
    platform::installStopSignals();
    platform::clearStopRequest();
    CHECK_FALSE(platform::stopRequested());
    REQUIRE(std::raise(SIGINT) == 0);
    CHECK(platform::stopRequested());
    platform::clearStopRequest();
    CHECK_FALSE(platform::stopRequested());
}

TEST_CASE("a termination request asks the same way")
{
    platform::installStopSignals();
    platform::clearStopRequest();
    REQUIRE(std::raise(SIGTERM) == 0);
    CHECK(platform::stopRequested());
    platform::clearStopRequest();
}
