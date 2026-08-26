#include "luaug/core/i18n.h"

#include "luaug/core/json.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace luaug::core {
namespace {

// Catalog diagnostics name the offending key rather than a byte offset: the
// grammar is already checked by the reader, so what is left to report is a
// schema violation, and "entry X" is what the person fixing it searches for.
std::string entryDiagnostic(std::string_view sourceName, std::string_view key, std::string_view what)
{
    return std::string{sourceName} + ": entry \"" + std::string{key} + "\" " + std::string{what};
}

// CLDR cardinal plural rules, as the families that actually differ (S6.9).
//
// **A SUBSET, and it says which.** The full CLDR set is a few hundred locales
// and a rule language to express them in; what a game needs is the handful of
// SHAPES those rules take, because the languages games are translated into fall
// into about seven of them. A language this does not name falls back to
// English's rule, which is the behaviour every locale had before -- so nothing
// gets worse and the named ones get right.
//
// Integers only. `count` is an `i64` all the way from `I18nArg`, so CLDR's
// fractional operands (`f`, `t`, `v`, `w`) cannot arise and the rules below are
// the integer projections of the published ones.
//
// The categories are CLDR's own words -- `zero`, `one`, `two`, `few`, `many`,
// `other` -- because those are the keys a translator writes in the catalog and
// a second vocabulary would be a second thing to get wrong.
std::string_view pluralCategory(std::string_view language, i64 count) noexcept
{
    // Negative counts pluralise as their magnitude in every rule CLDR states,
    // and "-1 item" reading as "-1 items" is the kind of thing nobody notices
    // until a refund appears in a shop.
    const i64 n = count < 0 ? -count : count;
    const i64 mod10 = n % 10;
    const i64 mod100 = n % 100;

    // No plural at all: one form for every count. Getting this wrong is the most
    // visible of the lot, because the translator has ONE string and the engine
    // asks for a category that is not in the file.
    for (const std::string_view none : {"ja", "zh", "ko", "vi", "th", "id", "ms", "my", "km", "lo"}) {
        if (language == none)
            return "other";
    }

    // One form for 0 and 1. French, Brazilian Portuguese, Hindi -- "0 jour"
    // rather than "0 jours".
    for (const std::string_view zeroIsOne : {"fr", "pt", "hi", "bn", "fa", "am", "gu", "kn", "mr", "zu"}) {
        if (language == zeroIsOne)
            return n == 0 || n == 1 ? "one" : "other";
    }

    // Russian, Ukrainian, Serbo-Croatian: one / few / many, by the last digit
    // and the last two.
    for (const std::string_view slavic : {"ru", "uk", "be", "sr", "hr", "bs"}) {
        if (language == slavic) {
            if (mod10 == 1 && mod100 != 11)
                return "one";
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                return "few";
            return "many";
        }
    }

    // Polish: the same shape with a different `other`, and CLDR gives it `many`
    // rather than `other` for the remaining case -- so it is spelled out rather than
    // folded into the block above.
    if (language == "pl") {
        if (n == 1)
            return "one";
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
            return "few";
        return "many";
    }

    // Czech and Slovak: one / few / other, and 2-4 is few only at exactly 2-4.
    if (language == "cs" || language == "sk") {
        if (n == 1)
            return "one";
        if (n >= 2 && n <= 4)
            return "few";
        return "other";
    }

    // Lithuanian, and Latvian's zero.
    if (language == "lt") {
        if (mod10 == 1 && (mod100 < 11 || mod100 > 19))
            return "one";
        if (mod10 >= 2 && mod10 <= 9 && (mod100 < 11 || mod100 > 19))
            return "few";
        return "other";
    }
    if (language == "lv") {
        if (mod10 == 0 || (mod100 >= 11 && mod100 <= 19))
            return "zero";
        if (mod10 == 1 && mod100 != 11)
            return "one";
        return "other";
    }

    // Arabic, which is the six-category case and the reason `zero` and `two`
    // exist as categories at all.
    if (language == "ar") {
        if (n == 0)
            return "zero";
        if (n == 1)
            return "one";
        if (n == 2)
            return "two";
        if (mod100 >= 3 && mod100 <= 10)
            return "few";
        if (mod100 >= 11 && mod100 <= 99)
            return "many";
        return "other";
    }

    // Romanian: one / few / other, where `few` covers 0 and the awkward teens.
    if (language == "ro") {
        if (n == 1)
            return "one";
        if (n == 0 || (mod100 >= 1 && mod100 <= 19))
            return "few";
        return "other";
    }

    // English, and every language this subset does not name. Spanish, German,
    // Italian, Dutch, Swedish and most of Europe share it exactly.
    return n == 1 ? std::string_view{"one"} : std::string_view{"other"};
}

} // namespace

I18nArg::I18nArg(std::string_view name, std::string_view value) : name_(name), value_(value)
{}

I18nArg::I18nArg(std::string_view name, i64 value) : name_(name), count_(value), hasCount_(true)
{
    value_ = std::to_string(value);
}

I18nArg::I18nArg(std::string_view name, f64 value) : name_(name)
{
    // Trim a trailing ".0" so counts and measurements read naturally in prose.
    std::string text = std::to_string(value);
    const usize dot = text.find('.');
    if (dot != std::string::npos) {
        usize last = text.find_last_not_of('0');
        if (last == dot)
            last = dot - 1;
        text.erase(last + 1);
    }
    value_ = std::move(text);
}

Catalog::LoadResult Catalog::loadFromJson(std::string_view json, std::string_view sourceName)
{
    JsonDocument document;
    const JsonDocument::ParseResult parsed = document.parse(json, sourceName);
    if (!parsed.ok)
        return LoadResult{false, parsed.diagnostic};

    const JsonValue root = document.root();
    if (root.type() != JsonType::Object)
        return LoadResult{false, std::string{sourceName} + ": a catalog must be a JSON object"};

    // The grammar the reader accepts is wider than the schema a catalog may use
    // (ADR 0033): a value is a message or a table of plural categories, and
    // anything else -- a number, an array, a deeper table -- is a mistake worth
    // a diagnostic rather than a translation that silently goes missing.
    decltype(entries_) loaded;

    for (usize index = 0; index < root.size(); ++index) {
        const std::string_view name = root.keyAt(index);
        const JsonValue value = root.at(index);

        std::string single;
        std::vector<std::pair<std::string, std::string>> plurals;

        if (value.type() == JsonType::String) {
            single = std::string{value.asString()};
        }
        else if (value.type() == JsonType::Object) {
            if (value.size() == 0)
                return LoadResult{false, entryDiagnostic(sourceName, name, "has no plural categories")};

            for (usize category = 0; category < value.size(); ++category) {
                const JsonValue text = value.at(category);
                if (text.type() != JsonType::String)
                    return LoadResult{false,
                                      entryDiagnostic(sourceName, name, "maps a plural category to a non-string")};
                plurals.emplace_back(std::string{value.keyAt(category)}, std::string{text.asString()});
            }
        }
        else {
            return LoadResult{false, entryDiagnostic(sourceName, name, "must be a string or a plural object")};
        }

        // `$`-prefixed keys are file metadata, not messages -- validated like
        // any other entry, then dropped.
        if (name.empty() || name.front() == '$')
            continue;

        const u32 hash = hashTextKey(name);
        const auto existing = loaded.find(hash);
        if (existing != loaded.end()) {
            // Two keys sharing a hash would make one of them unreachable. Fail
            // loudly at load rather than mistranslate at runtime.
            return LoadResult{false, std::string{sourceName} + ": key hash collision between \"" +
                                         existing->second.name + "\" and \"" + std::string{name} + "\""};
        }
        loaded.emplace(hash, Entry{std::string{name}, std::move(single), std::move(plurals)});
    }

    entries_ = std::move(loaded);
    return LoadResult{true, {}};
}

void Catalog::setLocale(std::string_view locale)
{
    // The language subtag, lowercased. `pt-BR` is `pt` and `zh_Hans` is `zh`:
    // region does not change a cardinal rule in CLDR, and accepting both
    // separators means a caller never has to know which one this file wanted.
    locale_.clear();
    for (const char c : locale) {
        if (c == '-' || c == '_')
            break;
        locale_.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
}

Catalog::LoadResult Catalog::loadFromFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return LoadResult{false, "cannot open catalog: " + path.string()};

    // **The file's own name is the locale**, so `i18n/pl.json` is Polish with no
    // second place to say so and nothing to keep in step. A catalog loaded from
    // a string keeps whatever locale it was given, which for a test is none --
    // and none is English.
    setLocale(path.stem().string());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    return loadFromJson(text, path.string());
}

bool Catalog::contains(TextKey key) const noexcept
{
    return entries_.find(key.hash) != entries_.end();
}

std::string_view Catalog::keyName(TextKey key) const noexcept
{
    const auto it = entries_.find(key.hash);
    return it == entries_.end() ? std::string_view{} : std::string_view{it->second.name};
}

std::string Catalog::format(TextKey key, std::span<const I18nArg> args) const
{
    const auto it = entries_.find(key.hash);
    if (it == entries_.end()) {
        // Visible, greppable, and carries the only identity we have. The i18n
        // lint prevents this from reaching a release.
        char marker[32];
        std::snprintf(marker, sizeof(marker), "[i18n:missing:%08x]", key.hash);
        return marker;
    }

    const Entry& entry = it->second;

    std::string_view templateText = entry.single;
    if (!entry.plurals.empty()) {
        i64 count = 0;
        for (const I18nArg& arg : args) {
            if (arg.hasCount() && arg.name() == "count") {
                count = arg.count();
                break;
            }
        }

        const std::string_view wanted = pluralCategory(locale_, count);
        const std::pair<std::string, std::string>* fallback = nullptr;
        for (const auto& plural : entry.plurals) {
            if (plural.first == wanted) {
                templateText = plural.second;
                fallback = nullptr;
                break;
            }
            if (fallback == nullptr || plural.first == "other")
                fallback = &plural;
        }
        if (fallback != nullptr)
            templateText = fallback->second;
    }

    std::string out;
    out.reserve(templateText.size() + 32);

    for (usize i = 0; i < templateText.size();) {
        if (templateText[i] != '{') {
            out.push_back(templateText[i]);
            ++i;
            continue;
        }

        const usize close = templateText.find('}', i + 1);
        if (close == std::string_view::npos) {
            out.append(templateText.substr(i));
            break;
        }

        const std::string_view name = templateText.substr(i + 1, close - i - 1);
        const I18nArg* found = nullptr;
        for (const I18nArg& arg : args) {
            if (arg.name() == name) {
                found = &arg;
                break;
            }
        }

        // An unsupplied placeholder is left verbatim: that makes the omission
        // visible to whoever reads the message instead of producing a
        // confident-looking sentence with a hole in it.
        if (found != nullptr)
            out.append(found->value());
        else
            out.append(templateText.substr(i, close - i + 1));

        i = close + 1;
    }

    return out;
}

Catalog& engineCatalog()
{
    static Catalog catalog;
    return catalog;
}

} // namespace luaug::core
