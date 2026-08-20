// The dev-server control connection (ADR 0035, M3 brief Decision 1 and the
// protocol section beside it).
//
// The engine dials out to `luaug dev`'s WebSocket server and speaks one JSON
// object per text frame. Only the dev server listens; this opens no port, and
// the whole file compiles into dev builds only.
//
// All socket I/O happens on this object's own thread. The frame loop never
// touches the connection: it takes whatever arrived at the FrameStart safe
// point and posts replies, which the thread sends. That is what keeps a reload
// from landing mid-tick, which is the rule architecture §4 states and the one
// that makes within-run determinism survive having a watcher attached.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::app
{

struct DevControlOptions
{
    // `ws://127.0.0.1:<port>/<path>`.
    std::string url;
    // Proves this connection is the engine `luaug dev` launched. A loopback
    // listener is still reachable by every process on the machine.
    std::string token;

    // The engine may start before the dev server is listening, so a refused
    // connection is retried rather than fatal.
    core::u32 connectAttempts = 20;
    core::u32 retryDelayMs = 100;
};

struct DevCommand
{
    enum class Kind
    {
        Reload,
        Sample,
        Ping,
        Shutdown,
        // A type this build does not implement -- `asset-changed` and `eval`
        // are reserved by the protocol -- or one it does not recognise at all.
        // Answered rather than ignored.
        Unsupported,
    };

    Kind kind = Kind::Unsupported;
    core::u64 id = 0;
    // `sample`: the world tick to answer at. ABSOLUTE, not a delta: a reload
    // restarts the tick counter, so "tick 40" is the only thing that means the
    // same in two different worlds -- which is what makes a reloaded world
    // comparable to a cold boot at all (M3 brief, Decision 11).
    core::u64 atTick = 0;
    // As received, so an `error` reply can name what it refused.
    std::string type;
    // `reload`: the rescan's real changed-file list.
    std::vector<std::string> paths;
};

class DevControl
{
public:
    DevControl();
    ~DevControl();

    DevControl(const DevControl&) = delete;
    DevControl& operator=(const DevControl&) = delete;

    // Connects, completes the handshake, sends `hello`, and starts the worker.
    // Returns the error that stopped it; the engine treats that as fatal,
    // because a `luaug dev` whose engine is not attached is a dev loop that
    // silently never reloads.
    [[nodiscard]] std::optional<core::EngineError> start(const DevControlOptions& options);

    // Everything that arrived since the last call, in arrival order. Call at
    // the FrameStart safe point and nowhere else.
    void takeCommands(std::vector<DevCommand>& out);

    // Queues one already-serialised JSON object. Sent by the worker thread, so
    // this never blocks the frame.
    void post(std::string message);

    [[nodiscard]] bool connected() const noexcept;

    // Sends nothing further and joins the worker. Safe to call twice.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace luaug::app
