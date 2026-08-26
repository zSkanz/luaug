// Key + catalog localisation (ADR 0019, rule R3).
//
// Contract: engine code raises (key, params) records and never concatenates
// user-facing text. Adding a locale is adding a catalog file -- there is no
// code path that hardcodes English.
//
// v1 scope: flat JSON per locale, `{param}` placeholders, CLDR plural
// categories selected by a `count` parameter. ICU-grade formatting, dates and
// gender are reserved.
//
// **Plural rules were English's for every locale until S6.9**, which ADR 0019
// accepted at launch on the grounds that English was the only catalog shipped.
// It is now a CLDR SUBSET keyed by language subtag -- see `setLocale` -- because
// the cost of getting it wrong falls entirely on the translator: a catalog that
// offers `few` and never selects it reads as broken grammar to the people it was
// written for, and there is nothing they can do about it from their side.
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

    // --- Which language's plural rules apply (S6.9) --------------------------
    //
    // **A catalog knows its own text and did not know its own language**, so
    // every locale was pluralised by English's rule -- one or other. That is
    // right for about half the languages anybody translates a game into and
    // wrong for the rest in a way a translator cannot work around: Polish needs
    // three forms, Russian three, Arabic six, and Japanese one. A catalog that
    // offers `few` and never selects it is a translation that reads as broken
    // grammar to the people it was written for.
    //
    // The BCP-47 language subtag, lowercased -- `pt` out of `pt-BR`, `zh` out of
    // `zh-Hans`. Region does not change a cardinal rule in CLDR, and the two
    // cases where a script arguably does are not ones this subset distinguishes.
    //
    // `loadFromFile` takes it from the file's stem, so `i18n/pl.json` is Polish
    // with no second place to say so. Empty is English, which is what a catalog
    // built in a test or from a string is.
    void setLocale(std::string_view locale);
    [[nodiscard]] std::string_view locale() const noexcept { return locale_; }

private:
    std::string locale_;

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
