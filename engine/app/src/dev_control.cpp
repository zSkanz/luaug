#include "luaug/app/dev_control.h"

#include "luaug/core/build_info.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/net/websocket.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace luaug::app {
namespace {

using core::I18nArg;

// Short enough that `stop` is not felt and that a queued reply leaves promptly,
// long enough that an idle connection is not a spin loop.
constexpr core::u32 kPollMs = 20;

[[nodiscard]] DevCommand::Kind kindOf(std::string_view type)
{
    if (type == "reload")
        return DevCommand::Kind::Reload;
    if (type == "sample")
        return DevCommand::Kind::Sample;
    if (type == "ping")
        return DevCommand::Kind::Ping;
    if (type == "shutdown")
        return DevCommand::Kind::Shutdown;
    return DevCommand::Kind::Unsupported;
}

// `ws://host:port/path`. Anything else is a usage error rather than something
// to guess at: the only thing that writes this string is `luaug dev`.
[[nodiscard]] bool parseUrl(std::string_view url, std::string& host, core::u16& port, std::string& path)
{
    constexpr std::string_view kScheme = "ws://";
    if (!url.starts_with(kScheme))
        return false;
    url.remove_prefix(kScheme.size());

    const std::size_t slash = url.find('/');
    const std::string_view authority = url.substr(0, slash);
    path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));

    const std::size_t colon = authority.find(':');
    if (colon == std::string_view::npos)
        return false;

    host = std::string(authority.substr(0, colon));
    const std::string_view portText = authority.substr(colon + 1);
    if (host.empty() || portText.empty())
        return false;

    core::u32 value = 0;
    for (const char digit : portText) {
        if (digit < '0' || digit > '9')
            return false;
        value = value * 10 + static_cast<core::u32>(digit - '0');
        if (value > 65535)
            return false;
    }
    port = static_cast<core::u16>(value);
    return port != 0;
}

} // namespace

struct DevControl::Impl
{
    net::WebSocketClient socket;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> live{false};

    std::mutex inboundLock;
    std::vector<DevCommand> inbound;

    std::mutex outboundLock;
    std::vector<std::string> outbound;

    void run();
    void ingest(const std::string& text);
};

void DevControl::Impl::ingest(const std::string& text)
{
    core::JsonDocument document;
    if (!document.parse(text)) {
        // Not fatal on its own: the connection is still framed correctly, and
        // one unreadable message is the dev server's bug to fix. Saying so is
        // the difference between that and a reload that never arrives.
        const std::array<I18nArg, 1> args{I18nArg{"detail", std::string_view{"the message is not valid JSON"}}};
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.dev.warn.bad_message"), args);
        return;
    }

    const core::JsonValue root = document.root();

    DevCommand command;
    command.type = std::string(root["type"].asString());
    command.kind = kindOf(command.type);
    command.id = static_cast<core::u64>(root["id"].asInteger(0));
    command.atTick = static_cast<core::u64>(root["atTick"].asInteger(0));

    const core::JsonValue paths = root["paths"];
    for (core::usize i = 0; i < paths.size(); ++i)
        command.paths.emplace_back(paths.at(i).asString());

    const std::lock_guard<std::mutex> guard(inboundLock);
    inbound.push_back(std::move(command));
}

void DevControl::Impl::run()
{
    while (running.load(std::memory_order_relaxed)) {
        // Replies first: a `reloaded` the frame loop posted should not wait a
        // poll interval behind an idle read.
        std::vector<std::string> pending;
        {
            const std::lock_guard<std::mutex> guard(outboundLock);
            pending.swap(outbound);
        }
        for (const std::string& message : pending) {
            if (auto error = socket.sendText(message)) {
                core::logText(core::LogLevel::Warn, error->message);
                live.store(false, std::memory_order_relaxed);
                running.store(false, std::memory_order_relaxed);
                return;
            }
        }

        std::string received;
        bool got = false;
        if (auto error = socket.receiveText(received, got, kPollMs)) {
            // Includes the orderly close: `luaug dev` going away ends the
            // connection and the engine keeps running, windowed, with no
            // watcher. Ending the process instead would make closing the dev
            // server look like a crash.
            core::logText(core::LogLevel::Info, error->message);
            live.store(false, std::memory_order_relaxed);
            running.store(false, std::memory_order_relaxed);
            return;
        }
        if (got)
            ingest(received);
    }
}

DevControl::DevControl() : m_impl(std::make_unique<Impl>())
{}

DevControl::~DevControl()
{
    stop();
}

std::optional<core::EngineError> DevControl::start(const DevControlOptions& options)
{
    net::WebSocketClientOptions socketOptions;
    if (!parseUrl(options.url, socketOptions.host, socketOptions.port, socketOptions.path)) {
        const std::array<I18nArg, 1> args{I18nArg{"url", options.url}};
        return core::makeError(LUAUG_TR("engine.dev.err.bad_url"), args);
    }

    std::optional<core::EngineError> lastError;
    for (core::u32 attempt = 0; attempt < options.connectAttempts; ++attempt) {
        lastError = m_impl->socket.connect(socketOptions);
        if (!lastError.has_value())
            break;
        // The engine is usually started BY the dev server, so losing the race
        // to its listener is the normal case rather than the exception.
        std::this_thread::sleep_for(std::chrono::milliseconds(options.retryDelayMs));
    }
    if (lastError.has_value())
        return lastError;

    core::JsonWriter hello;
    hello.beginObject();
    hello.field("type", "hello");
    hello.field("id", static_cast<core::u64>(1));
    hello.field("protocol", static_cast<core::u64>(1));
    hello.field("role", "engine");
    hello.field("token", options.token);
    hello.field("engine", LUAUG_VERSION_STRING);
    hello.endObject();

    if (auto error = m_impl->socket.sendText(hello.text()))
        return error;

    m_impl->running.store(true, std::memory_order_relaxed);
    m_impl->live.store(true, std::memory_order_relaxed);
    m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
    return std::nullopt;
}

void DevControl::takeCommands(std::vector<DevCommand>& out)
{
    out.clear();
    const std::lock_guard<std::mutex> guard(m_impl->inboundLock);
    out.swap(m_impl->inbound);
}

void DevControl::post(std::string message)
{
    const std::lock_guard<std::mutex> guard(m_impl->outboundLock);
    m_impl->outbound.push_back(std::move(message));
}

bool DevControl::connected() const noexcept
{
    return m_impl->live.load(std::memory_order_relaxed);
}

void DevControl::stop()
{
    if (!m_impl)
        return;

    m_impl->running.store(false, std::memory_order_relaxed);
    if (m_impl->worker.joinable())
        m_impl->worker.join();

    // After the join, because the worker is the only thing allowed to touch
    // the socket while it is alive.
    m_impl->socket.close();
    m_impl->live.store(false, std::memory_order_relaxed);
}

} // namespace luaug::app
