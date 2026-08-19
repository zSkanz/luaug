#include "luaug/core/i18n.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace luaug::core
{
namespace
{

// A deliberately small JSON reader, private to the catalog.
//
// Catalogs are a closed, flat grammar -- object of string -> (string | object
// of string) -- so accepting arrays, numbers or booleans would only widen the
// surface without serving a caller. Rejecting them produces a precise
// diagnostic instead of a silently mis-parsed catalog. When the engine needs
// general JSON it will be a deliberate, separate decision (and a dependency
// ADR), not an accident of this file growing.
class CatalogReader
{
public:
    explicit CatalogReader(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    template<typename OnEntry>
    bool parse(OnEntry&& onEntry)
    {
        skipSpace();
        if (!expect('{'))
            return false;

        skipSpace();
        if (peek() == '}')
        {
            advance();
            return atEndAfterTrailingSpace();
        }

        for (;;)
        {
            skipSpace();
            std::string key;
            if (!readString(key))
                return false;

            skipSpace();
            if (!expect(':'))
                return false;

            skipSpace();
            const char next = peek();
            if (next == '"')
            {
                std::string value;
                if (!readString(value))
                    return false;
                if (!key.empty() && key.front() != '$')
                    onEntry(key, std::move(value), std::vector<std::pair<std::string, std::string>>{});
            }
            else if (next == '{')
            {
                std::vector<std::pair<std::string, std::string>> plurals;
                if (!readPluralObject(plurals))
                    return false;
                if (!key.empty() && key.front() != '$')
                    onEntry(key, std::string{}, std::move(plurals));
            }
            else
            {
                return fail("catalog values must be a string or a plural object");
            }

            skipSpace();
            const char delim = peek();
            if (delim == ',')
            {
                advance();
                continue;
            }
            if (delim == '}')
            {
                advance();
                return atEndAfterTrailingSpace();
            }
            return fail("expected ',' or '}' after a catalog entry");
        }
    }

private:
    [[nodiscard]] char peek() const noexcept { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    void advance() noexcept
    {
        if (pos_ < text_.size())
            ++pos_;
    }

    void skipSpace() noexcept
    {
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool expect(char c)
    {
        if (peek() != c)
        {
            char message[64];
            std::snprintf(message, sizeof(message), "expected '%c'", c);
            return fail(message);
        }
        advance();
        return true;
    }

    bool atEndAfterTrailingSpace()
    {
        skipSpace();
        if (pos_ != text_.size())
            return fail("trailing content after the catalog object");
        return true;
    }

    bool fail(std::string_view what)
    {
        if (error_.empty())
        {
            std::ostringstream out;
            out << what << " at byte " << pos_;
            error_ = out.str();
        }
        return false;
    }

    static void appendUtf8(std::string& out, u32 codepoint)
    {
        if (codepoint < 0x80u)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint < 0x800u)
        {
            out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else if (codepoint < 0x10000u)
        {
            out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    }

    bool readHex4(u32& out)
    {
        if (pos_ + 4 > text_.size())
            return fail("truncated \\u escape");

        u32 value = 0;
        for (usize i = 0; i < 4; ++i)
        {
            const char c = text_[pos_ + i];
            u32 digit = 0;
            if (c >= '0' && c <= '9')
                digit = static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<u32>(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<u32>(c - 'A') + 10u;
            else
                return fail("invalid hex digit in \\u escape");
            value = (value << 4) | digit;
        }
        pos_ += 4;
        out = value;
        return true;
    }

    bool readString(std::string& out)
    {
        if (!expect('"'))
            return false;

        out.clear();
        for (;;)
        {
            if (pos_ >= text_.size())
                return fail("unterminated string");

            const char c = text_[pos_];
            if (c == '"')
            {
                advance();
                return true;
            }

            if (c != '\\')
            {
                out.push_back(c);
                advance();
                continue;
            }

            advance();
            if (pos_ >= text_.size())
                return fail("unterminated escape");

            const char esc = text_[pos_];
            advance();
            switch (esc)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                u32 codepoint = 0;
                if (!readHex4(codepoint))
                    return false;
                // Surrogate pair: catalogs are UTF-8, so re-combine before encoding.
                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu && pos_ + 1 < text_.size() && text_[pos_] == '\\'
                    && text_[pos_ + 1] == 'u')
                {
                    pos_ += 2;
                    u32 low = 0;
                    if (!readHex4(low))
                        return false;
                    if (low < 0xDC00u || low > 0xDFFFu)
                        return fail("invalid low surrogate in \\u escape");
                    codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                }
                appendUtf8(out, codepoint);
                break;
            }
            default:
                return fail("unsupported escape sequence");
            }
        }
    }

    bool readPluralObject(std::vector<std::pair<std::string, std::string>>& out)
    {
        if (!expect('{'))
            return false;

        skipSpace();
        if (peek() == '}')
        {
            advance();
            return fail("plural entry has no categories");
        }

        for (;;)
        {
            skipSpace();
            std::string category;
            if (!readString(category))
                return false;

            skipSpace();
            if (!expect(':'))
                return false;

            skipSpace();
            std::string value;
            if (!readString(value))
                return false;

            out.emplace_back(std::move(category), std::move(value));

            skipSpace();
            const char delim = peek();
            if (delim == ',')
            {
                advance();
                continue;
            }
            if (delim == '}')
            {
                advance();
                return true;
            }
            return fail("expected ',' or '}' in a plural entry");
        }
    }

    std::string_view text_;
    usize pos_ = 0;
    std::string error_;
};

// English CLDR rules, the only locale shipped at launch (ADR 0019). A locale
// with richer rules gets them when its catalog lands; the selection point is
// here so that is a local change.
std::string_view pluralCategory(i64 count) noexcept
{
    return count == 1 ? std::string_view{"one"} : std::string_view{"other"};
}

} // namespace

I18nArg::I18nArg(std::string_view name, std::string_view value) : name_(name), value_(value) {}

I18nArg::I18nArg(std::string_view name, i64 value) : name_(name), count_(value), hasCount_(true)
{
    value_ = std::to_string(value);
}

I18nArg::I18nArg(std::string_view name, f64 value) : name_(name)
{
    // Trim a trailing ".0" so counts and measurements read naturally in prose.
    std::string text = std::to_string(value);
    const usize dot = text.find('.');
    if (dot != std::string::npos)
    {
        usize last = text.find_last_not_of('0');
        if (last == dot)
            last = dot - 1;
        text.erase(last + 1);
    }
    value_ = std::move(text);
}

Catalog::LoadResult Catalog::loadFromJson(std::string_view json, std::string_view sourceName)
{
    decltype(entries_) parsed;
    std::string collision;

    CatalogReader reader(json);
    const bool ok = reader.parse(
        [&](const std::string& name, std::string&& single, std::vector<std::pair<std::string, std::string>>&& plurals)
        {
            const u32 hash = hashTextKey(name);
            const auto existing = parsed.find(hash);
            if (existing != parsed.end() && collision.empty())
            {
                // Two keys sharing a hash would make one of them unreachable.
                // Fail loudly at load rather than mistranslate at runtime.
                collision = existing->second.name + "\" and \"" + name;
                return;
            }
            parsed.emplace(hash, Entry{name, std::move(single), std::move(plurals)});
        });

    if (!ok)
        return LoadResult{false, std::string{sourceName} + ": " + reader.error()};

    if (!collision.empty())
        return LoadResult{false, std::string{sourceName} + ": key hash collision between \"" + collision + "\""};

    entries_ = std::move(parsed);
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
    if (it == entries_.end())
    {
        // Visible, greppable, and carries the only identity we have. The i18n
        // lint prevents this from reaching a release.
        char marker[32];
        std::snprintf(marker, sizeof(marker), "[i18n:missing:%08x]", key.hash);
        return marker;
    }

    const Entry& entry = it->second;

    std::string_view templateText = entry.single;
    if (!entry.plurals.empty())
    {
        i64 count = 0;
        for (const I18nArg& arg : args)
        {
            if (arg.hasCount() && arg.name() == "count")
            {
                count = arg.count();
                break;
            }
        }

        const std::string_view wanted = pluralCategory(count);
        const std::pair<std::string, std::string>* fallback = nullptr;
        for (const auto& plural : entry.plurals)
        {
            if (plural.first == wanted)
            {
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

    for (usize i = 0; i < templateText.size();)
    {
        if (templateText[i] != '{')
        {
            out.push_back(templateText[i]);
            ++i;
            continue;
        }

        const usize close = templateText.find('}', i + 1);
        if (close == std::string_view::npos)
        {
            out.append(templateText.substr(i));
            break;
        }

        const std::string_view name = templateText.substr(i + 1, close - i - 1);
        const I18nArg* found = nullptr;
        for (const I18nArg& arg : args)
        {
            if (arg.name() == name)
            {
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
