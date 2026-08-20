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

// English CLDR rules, the only locale shipped at launch (ADR 0019). A locale
// with richer rules gets them when its catalog lands; the selection point is
// here so that is a local change.
std::string_view pluralCategory(i64 count) noexcept
{
    return count == 1 ? std::string_view{"one"} : std::string_view{"other"};
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

Catalog::LoadResult Catalog::loadFromFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return LoadResult{false, "cannot open catalog: " + path.string()};

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

        const std::string_view wanted = pluralCategory(count);
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
