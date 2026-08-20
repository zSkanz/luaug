// The hot-reload state bag (ADR 0024, api-design.md §3.2, M3 brief Decision 4).
//
// The bag cannot live in Luau, because at the moment it matters the VM that
// held it is being destroyed: `lua_close` takes the table with it. So
// `SaveState` converts its argument into one of these at the moment of the
// call, and `LoadState` builds a fresh Luau value from it inside the world the
// reload produced.
//
// Owned by `app` -- it has to outlive `WorldHost`, which is the thing a reload
// destroys -- and reached from a binding through `ServiceState`.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "luaug/core/types.h"

struct lua_State;

namespace luaug::script
{

using core::f64;
using core::u8;
using core::usize;

// A value that outlives the VM that produced it. Deliberately the JSON-able set
// plus `buffer`, which is what api-design.md §3.2 promises and no more: a
// function is a closure over a VM that is about to stop existing, and an
// `Instance` is a handle into a world that is about to be rebuilt. Neither can
// be copied forward under any interpretation, so neither is accepted.
class BagValue
{
public:
    // Insertion-ordered rather than hashed, both here and in `ReloadState`.
    // Restoring in a different order than saving would make two runs of the
    // same reload build different worlds, which is exactly what R10 forbids a
    // container from deciding.
    using Array = std::vector<BagValue>;
    using Map = std::vector<std::pair<std::string, BagValue>>;
    using Bytes = std::vector<u8>;

    using Storage = std::variant<std::monostate, bool, f64, std::string, Bytes, Array, Map>;

    BagValue() = default;
    explicit BagValue(Storage value) : m_value(std::move(value)) {}

    [[nodiscard]] const Storage& storage() const noexcept { return m_value; }
    [[nodiscard]] bool isNil() const noexcept { return std::holds_alternative<std::monostate>(m_value); }

private:
    Storage m_value;
};

class ReloadState
{
public:
    // Whether the world reading this was built by a reload rather than by
    // starting the engine. Set by the host on the incoming world, and the whole
    // of what `HotReloadService:IsReload` answers.
    [[nodiscard]] bool isReload() const noexcept { return m_isReload; }
    void setIsReload(bool value) noexcept { m_isReload = value; }

    // Replaces any earlier value under the same key, keeping the key's original
    // position: a script that saves twice has not reordered the bag.
    void save(std::string_view key, BagValue value);

    [[nodiscard]] const BagValue* load(std::string_view key) const;

    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }
    void clear() noexcept { m_entries.clear(); }

private:
    std::vector<std::pair<std::string, BagValue>> m_entries;
    bool m_isReload = false;
};

// Converts the Luau value at `index` into a copy that outlives this VM. Returns
// nothing and fills `reason` when the value cannot cross -- a function, a
// thread, an Instance or any other userdata, a table keyed by anything but
// strings or a 1..n run of integers, a cycle, or a nesting deeper than the
// engine will walk.
//
// `index` must be absolute. The conversion pushes while it recurses, and a
// relative index would move under it.
[[nodiscard]] std::optional<BagValue> toBagValue(lua_State* L, int index, std::string& reason);

// Builds a fresh Luau value from `value` and leaves it on the stack. Fresh, and
// not shared: mutating what `LoadState` returned changes nothing a later reload
// will see, and only another `SaveState` does.
void pushBagValue(lua_State* L, const BagValue& value);

} // namespace luaug::script
