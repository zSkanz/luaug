#include "luaug/app/script_editor.h"

#include "luaug/scene/world.h"

#include <algorithm>
#include <tuple>

namespace luaug::app {

OpenScript& ScriptEditor::open(core::InstanceId instance, std::string chunk, std::string file, std::string title,
                               std::string_view source)
{
    if (const std::optional<std::size_t> found = indexOf(instance); found.has_value()) {
        // Already open: this is a focus, not a load. Re-seeding the text here
        // would throw away whatever somebody has been typing, which is the one
        // thing a second double-click must not do.
        m_active = *found;
        OpenScript& existing = m_tabs[*found];
        existing.chunk = std::move(chunk);
        existing.file = std::move(file);
        existing.title = std::move(title);
        return existing;
    }

    OpenScript tab;
    tab.instance = instance;
    tab.chunk = std::move(chunk);
    tab.file = std::move(file);
    tab.title = std::move(title);
    (void)tab.document.setText(source);
    tab.savedRevision = tab.document.revision();

    m_tabs.push_back(std::move(tab));
    m_active = m_tabs.size() - 1;
    return m_tabs.back();
}

bool ScriptEditor::close(std::size_t index)
{
    if (index >= m_tabs.size())
        return false;

    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_tabs.empty()) {
        m_active = 0;
        return true;
    }
    // The one to its left, which is where the eye already was. Closing the first
    // tab leaves the first tab in front, which is the same rule read from the
    // other end.
    if (m_active > index || m_active >= m_tabs.size())
        m_active = m_active > 0 ? m_active - 1 : 0;
    return true;
}

void ScriptEditor::closeAll()
{
    m_tabs.clear();
    m_active = 0;
}

OpenScript* ScriptEditor::at(std::size_t index) noexcept
{
    return index < m_tabs.size() ? &m_tabs[index] : nullptr;
}

const OpenScript* ScriptEditor::at(std::size_t index) const noexcept
{
    return index < m_tabs.size() ? &m_tabs[index] : nullptr;
}

OpenScript* ScriptEditor::active() noexcept
{
    return at(m_active);
}

void ScriptEditor::setActive(std::size_t index) noexcept
{
    if (index < m_tabs.size())
        m_active = index;
}

std::optional<std::size_t> ScriptEditor::indexOf(core::InstanceId instance) const noexcept
{
    for (std::size_t index = 0; index < m_tabs.size(); ++index) {
        if (m_tabs[index].instance == instance)
            return index;
    }
    return std::nullopt;
}

bool ScriptEditor::anyDirty() const noexcept
{
    return std::any_of(m_tabs.begin(), m_tabs.end(), [](const OpenScript& tab) { return tab.dirty(); });
}

std::size_t ScriptEditor::dirtyCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(m_tabs.begin(), m_tabs.end(), [](const OpenScript& tab) { return tab.dirty(); }));
}

void ScriptEditor::markSaved(std::size_t index) noexcept
{
    if (OpenScript* tab = at(index); tab != nullptr)
        tab->savedRevision = tab->document.revision();
}

std::size_t ScriptEditor::forgetDestroyed(const scene::World& world)
{
    std::size_t closed = 0;
    for (std::size_t index = m_tabs.size(); index > 0; --index) {
        if (world.alive(m_tabs[index - 1].instance))
            continue;
        (void)close(index - 1);
        ++closed;
    }
    return closed;
}

// --- Breakpoints -------------------------------------------------------------

namespace {

[[nodiscard]] bool orderBreakpoints(const Breakpoint& a, const Breakpoint& b) noexcept
{
    return std::tie(a.chunk, a.line) < std::tie(b.chunk, b.line);
}

} // namespace

bool ScriptEditor::toggleBreakpoint(std::string_view chunk, core::u32 line)
{
    const auto found = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                    [&](const Breakpoint& bp) { return bp.chunk == chunk && bp.line == line; });
    if (found != m_breakpoints.end()) {
        m_breakpoints.erase(found);
        return false;
    }

    m_breakpoints.push_back(Breakpoint{.chunk = std::string(chunk), .line = line});
    // Sorted on insert rather than on read, so every walk of this list -- the
    // panel's, the debugger's, the one that re-binds after a reload -- is in the
    // same order without any of them having to say so (R10).
    std::sort(m_breakpoints.begin(), m_breakpoints.end(), orderBreakpoints);
    return true;
}

void ScriptEditor::clearBreakpoints(std::string_view chunk)
{
    m_breakpoints.erase(std::remove_if(m_breakpoints.begin(), m_breakpoints.end(),
                                       [&](const Breakpoint& bp) { return bp.chunk == chunk; }),
                        m_breakpoints.end());
}

bool ScriptEditor::hasBreakpoint(std::string_view chunk, core::u32 line) const noexcept
{
    return std::any_of(m_breakpoints.begin(), m_breakpoints.end(),
                       [&](const Breakpoint& bp) { return bp.chunk == chunk && bp.line == line; });
}

void ScriptEditor::setBoundLine(std::string_view chunk, core::u32 line, core::u32 boundLine) noexcept
{
    for (Breakpoint& bp : m_breakpoints) {
        if (bp.chunk == chunk && bp.line == line)
            bp.boundLine = boundLine;
    }
}

} // namespace luaug::app
