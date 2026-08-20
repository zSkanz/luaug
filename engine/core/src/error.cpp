#include "luaug/core/error.h"

#include <cstdio>
#include <utility>

namespace luaug::core {

std::string formatKeyPrefixed(TextKey key, std::span<const I18nArg> args)
{
    const Catalog& catalog = engineCatalog();
    const std::string_view name = catalog.keyName(key);

    std::string out;
    out.reserve(name.size() + 48);
    out.push_back('[');
    if (name.empty()) {
        // Keep the hash visible so an unregistered key is still traceable to
        // its call site; the i18n lint stops this reaching a release.
        char marker[24];
        std::snprintf(marker, sizeof(marker), "i18n:missing:%08x", key.hash);
        out.append(marker);
    }
    else {
        out.append(name);
    }
    out.append("] ");
    out.append(catalog.format(key, args));
    return out;
}

EngineError makeError(TextKey key, std::span<const I18nArg> args, std::string detail)
{
    return EngineError{key, formatKeyPrefixed(key, args), std::move(detail)};
}

} // namespace luaug::core
