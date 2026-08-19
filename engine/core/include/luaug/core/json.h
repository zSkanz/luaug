// A small, strict JSON reader (ADR 0033).
//
// It parses; it does not serialize. Nothing in the engine writes JSON — the
// generated files are written by CMake, and `tools/repo/vendor.luau`
// deliberately never rewrites the manifest — so a writer would be surface with
// no caller.
//
// Three properties are the point, and they are inherited from the restricted
// catalog reader this replaces:
//
//   Diagnostics name a byte offset and what was expected. A malformed manifest
//   fails at load, and the message is the whole value of failing there.
//
//   Nesting is bounded. A corrupt or hostile file cannot recurse the parser
//   into the stack guard.
//
//   Nothing is coerced. Asking a string for a number is an error, not a zero.
#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "luaug/core/types.h"

namespace luaug::core
{

enum class JsonType : u8
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

class JsonValue;

// Owns the whole document. Values are non-owning views into it, so a document
// must outlive every value read out of it -- the same contract a parsed DOM
// always has, stated here because it is the one way to misuse this.
class JsonDocument
{
public:
    struct ParseResult
    {
        bool ok = false;
        // Developer-facing: a malformed document is a build or startup failure,
        // never something a player sees, so this is exempt from R3 the same way
        // Catalog::LoadResult::diagnostic is.
        std::string diagnostic;

        explicit operator bool() const noexcept { return ok; }
    };

    JsonDocument();
    ~JsonDocument();

    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    JsonDocument(JsonDocument&&) noexcept;
    JsonDocument& operator=(JsonDocument&&) noexcept;

    // `sourceName` appears in diagnostics only.
    ParseResult parse(std::string_view json, std::string_view sourceName = "<json>");

    // Null when parse() has not succeeded.
    [[nodiscard]] JsonValue root() const noexcept;

private:
    friend class JsonValue;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A view into a parsed document. Copyable and cheap; every accessor is total,
// returning a default or an empty value rather than throwing, so reading a
// well-formed document with an unexpected shape produces a missing field rather
// than a crash. Callers that need the difference ask `type()` or `has()`.
class JsonValue
{
public:
    JsonValue() = default;

    [[nodiscard]] JsonType type() const noexcept;
    [[nodiscard]] bool isNull() const noexcept { return type() == JsonType::Null; }
    [[nodiscard]] explicit operator bool() const noexcept { return node_ != nullptr; }

    // Scalars. Each returns `fallback` when this value is not of that type --
    // never a coerced conversion, which is how a typo in a manifest turns into
    // a plausible-looking zero.
    [[nodiscard]] bool asBool(bool fallback = false) const noexcept;
    [[nodiscard]] f64 asNumber(f64 fallback = 0.0) const noexcept;
    [[nodiscard]] i64 asInteger(i64 fallback = 0) const noexcept;
    // Escapes are already decoded, so the view points into storage the document
    // owns rather than into the original text.
    [[nodiscard]] std::string_view asString(std::string_view fallback = {}) const noexcept;

    // Arrays. Out-of-range yields a null value.
    [[nodiscard]] usize size() const noexcept;
    [[nodiscard]] JsonValue at(usize index) const noexcept;

    // Objects. Key order is preserved as written, because a caller that
    // iterates a generated file and compares the result to a golden must not
    // depend on a hash order.
    [[nodiscard]] bool has(std::string_view key) const noexcept;
    [[nodiscard]] JsonValue operator[](std::string_view key) const noexcept;
    [[nodiscard]] std::string_view keyAt(usize index) const noexcept;

private:
    friend class JsonDocument;

    struct Node;
    explicit JsonValue(const Node* node, const JsonDocument::Impl* owner) noexcept
        : node_(node)
        , owner_(owner)
    {
    }

    const Node* node_ = nullptr;
    const JsonDocument::Impl* owner_ = nullptr;
};

} // namespace luaug::core
