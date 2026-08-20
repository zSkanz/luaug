// Key + catalog localisation (ADR 0019, rule R3).
//
// Contract: engine code raises (key, params) records and never concatenates
// user-facing text. Adding a locale is adding a catalog file -- there is no
// code path that hardcodes English.
//
// v1 scope: flat JSON per locale, `{param}` placeholders, CLDR plural
// categories selected by a `count` parameter. English plural rules only at
// launch (ADR 0019); ICU-grade formatting, dates and gender are reserved.
#pragma once

#include "luaug/core/text_key.h"
#include "luaug/core/types.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luaug::core {

// A named substitution for a `{param}` placeholder. Numeric overloads record
// the value as well as its text so plural selection can use it.
class I18nArg
{
public:
    I18nArg(std::string_view name, std::string_view value);
    I18nArg(std::string_view name, i64 value);
    I18nArg(std::string_view name, f64 value);

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::string_view value() const noexcept { return value_; }
    [[nodiscard]] bool hasCount() const noexcept { return hasCount_; }
    [[nodiscard]] i64 count() const noexcept { return count_; }

private:
    std::string_view name_;
    std::string value_;
    i64 count_ = 0;
    bool hasCount_ = false;
};

class Catalog
{
public:
    struct LoadResult
    {
        bool ok = false;
        // Developer-facing diagnostic (a malformed catalog is a build/CI
        // failure, never something an end user sees), so this is exempt from
        // R3 by construction.
        std::string diagnostic;

        explicit operator bool() const noexcept { return ok; }
    };

    // Replaces any previously loaded entries. `sourceName` appears in
    // diagnostics only.
    LoadResult loadFromJson(std::string_view json, std::string_view sourceName);
    LoadResult loadFromFile(const std::filesystem::path& path);

    [[nodiscard]] bool contains(TextKey key) const noexcept;

    // Empty when the key is absent; the i18n lint makes that a CI failure.
    [[nodiscard]] std::string_view keyName(TextKey key) const noexcept;

    // Resolves and substitutes. A key with no entry yields a visible
    // placeholder rather than empty text, so a miss is obvious in a
    // screenshot or a log instead of silently vanishing.
    [[nodiscard]] std::string format(TextKey key, std::span<const I18nArg> args = {}) const;

    [[nodiscard]] usize size() const noexcept { return entries_.size(); }

private:
    struct Entry
    {
        std::string name;
        std::string single;
        // CLDR category -> template, empty when the entry is not plural.
        std::vector<std::pair<std::string, std::string>> plurals;
    };

    std::unordered_map<u32, Entry> entries_;
};

// The process-wide engine catalog (`i18n/en.json`). Services, errors and the
// log all resolve through this one.
Catalog& engineCatalog();

} // namespace luaug::core
