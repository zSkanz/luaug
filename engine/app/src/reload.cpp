#include "luaug/app/preserved.h"
#include "luaug/app/reload.h"

#include <array>
#include <chrono>

namespace luaug::app
{
namespace
{

using core::I18nArg;

[[nodiscard]] core::f64 elapsedMs(std::chrono::steady_clock::time_point since)
{
    const auto delta = std::chrono::steady_clock::now() - since;
    return std::chrono::duration<core::f64, std::milli>(delta).count();
}

} // namespace

ReloadReport reloadWorld(std::unique_ptr<WorldHost>& host, const WorldHostOptions& options)
{
    const auto started = std::chrono::steady_clock::now();

    ReloadReport report;

    // On the outgoing world, and drained, because a `PreReload` handler that
    // has not run yet has not called `SaveState` yet. This is the last moment
    // anything in the old VM can put a value where the new one will find it.
    if (host)
        host->firePreReload();

    // Captured after `PreReload` has been drained, so a handler that moved or
    // tagged something did it before this reads the tree.
    std::vector<PreservedTree> preserved;
    if (host)
        preserved = capturePreserved(host->world(), host->runtime().dataModel(), report.preserve);

    // The incoming world is a reload by construction; nothing else calls this.
    WorldHostOptions freshOptions = options;
    freshOptions.isReload = true;
    freshOptions.preserved = &preserved;

    auto fresh = std::make_unique<WorldHost>();
    if (std::optional<core::EngineError> error = fresh->boot(freshOptions); error.has_value())
    {
        report.error = std::move(error);
        report.spanMs = elapsedMs(started);
        return report;
    }

    report.mountedScripts = fresh->mountedScriptCount();
    report.loadFailures = fresh->scriptLoadFailures();
    report.preserve.restored = fresh->preserveReport().restored;
    report.preserve.skipped += fresh->preserveReport().skipped;

    // An empty project path is an empty world on purpose -- it is what the
    // render gates and `--version` boot -- so the check is about a project
    // directory that stopped containing scripts, which means the developer just
    // broke or moved the whole tree. Keeping the world they had is more useful
    // than swapping in an empty one and calling it a reload.
    if (report.mountedScripts == 0 && !options.projectPath.empty())
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", options.projectPath.string()}};
        report.error = core::makeError(LUAUG_TR("engine.reload.err.no_scripts"), args);
        report.spanMs = elapsedMs(started);
        return report;
    }

    // A syntax error is the common case in a loop whose point is that you save
    // often, and the useful answer to it is the world the developer already
    // had. `startScripts` has logged which script and why.
    if (report.loadFailures != 0)
    {
        const std::array<I18nArg, 1> args{I18nArg{"count", static_cast<core::i64>(report.loadFailures)}};
        report.error = core::makeError(LUAUG_TR("engine.reload.err.script_failed"), args);
        report.spanMs = elapsedMs(started);
        return report;
    }

    // Only now is the old world unreachable. Everything above `WorldHost` --
    // the window, the device, the renderer -- never knew this happened.
    host = std::move(fresh);

    // After the swap and after boot's own drain, so a `PostReload` handler
    // connected at file scope sees a world that is finished rather than one
    // still being assembled.
    host->firePostReload();

    report.ok = true;
    report.spanMs = elapsedMs(started);
    return report;
}

} // namespace luaug::app
