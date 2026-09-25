// The script editor's language service (ADR 0093). See language_service.h.
#include "luaug/app/language_service.h"

#include "luaug/scene/world.h"

#include <Luau/AstQuery.h>
#include <Luau/Autocomplete.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/ConstraintSolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/LinterConfig.h>
#include <Luau/ToString.h>
#include <Luau/Type.h>
#include <Luau/TypePack.h>

#include <algorithm>
#include <map>
#include <regex>
#include <unordered_map>

namespace luaug::app {

using core::i32;
using core::u32;
using core::u64;

std::optional<u32> LanguageTree::find(std::string_view path) const
{
    for (u32 index = 0; index < nodes.size(); ++index) {
        if (nodes[index].path == path)
            return index;
    }
    return std::nullopt;
}

std::string LanguageTree::pathOf(core::InstanceId id) const
{
    for (const Node& node : nodes) {
        if (node.id == id && node.script)
            return node.path;
    }
    return {};
}

namespace {

[[nodiscard]] std::string_view classNameOf(const scene::World& world, core::InstanceId id)
{
    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    return descriptor != nullptr ? world.atoms().text(descriptor->name) : std::string_view{};
}

} // namespace

LanguageTree captureLanguageTree(const scene::World& world, core::InstanceId dataModel)
{
    LanguageTree tree;
    if (!world.alive(dataModel))
        return tree;

    // Document order, so a name two siblings share resolves to the first, as
    // `FindFirstChild` does at run time.
    std::vector<core::InstanceId> all{dataModel};
    world.collectDescendants(dataModel, all);

    // What a require can walk: the scripts and their ancestors.
    std::unordered_map<u64, bool> kept;
    const auto key = [](core::InstanceId id) { return (static_cast<u64>(id.index) << 32) | id.generation; };
    kept[key(dataModel)] = true;
    for (const core::InstanceId id : all) {
        const std::string_view name = classNameOf(world, id);
        if (name != "Script" && name != "ModuleScript")
            continue;
        for (core::InstanceId walk = id; walk.valid() && !kept[key(walk)]; walk = world.parentOf(walk))
            kept[key(walk)] = true;
    }

    std::unordered_map<u64, u32> indexOf;
    const core::NameAtom source = world.atoms().lookup("Source");
    for (const core::InstanceId id : all) {
        if (!kept[key(id)])
            continue;
        LanguageTree::Node node;
        node.id = id;
        node.name = std::string(world.atoms().text(world.name(id)));
        node.className = std::string(classNameOf(world, id));
        node.script = node.className == "Script" || node.className == "ModuleScript";
        node.module = node.className == "ModuleScript";
        if (id == dataModel) {
            node.path = "game";
        }
        else if (const auto parent = indexOf.find(key(world.parentOf(id))); parent != indexOf.end()) {
            node.parent = static_cast<i32>(parent->second);
            node.path = tree.nodes[parent->second].path + "." + node.name;
        }
        else {
            continue;
        }
        if (node.script && source.valid()) {
            if (const std::optional<scene::Value> text = world.getProperty(id, source); text.has_value()) {
                if (const auto* string = std::get_if<std::string>(&*text); string != nullptr)
                    node.source = *string;
            }
        }
        const u32 index = static_cast<u32>(tree.nodes.size());
        indexOf[key(id)] = index;
        if (node.parent >= 0)
            tree.nodes[static_cast<u32>(node.parent)].children.push_back(index);
        tree.nodes.push_back(std::move(node));
    }
    return tree;
}

namespace {

// --- Docs, read from the definitions' `---` comments -------------------------
//
// The definitions carry every member's prose above it (gen_dts), which is what
// luau-lsp shows on hover. Keyed `Owner.member`, or a bare name for a global
// function.
[[nodiscard]] std::unordered_map<std::string, std::string> indexDocs(std::string_view text)
{
    std::unordered_map<std::string, std::string> docs;
    std::string owner;
    std::string pending;
    const auto word = [](std::string_view line, std::size_t from) {
        std::size_t end = from;
        while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) != 0 || line[end] == '_'))
            ++end;
        return std::string(line.substr(from, end - from));
    };
    const auto startsWith = [](std::string_view line, std::string_view prefix) {
        return line.substr(0, prefix.size()) == prefix;
    };

    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const bool topLevel = !line.empty() && line.front() != ' ';
        std::string_view body = line;
        while (!body.empty() && body.front() == ' ')
            body.remove_prefix(1);

        // One paragraph per run of comment lines -- they are wrapped for the
        // file, not for the box that shows them -- and a blank one between.
        if (startsWith(body, "---")) {
            body.remove_prefix(3);
            if (!body.empty() && body.front() == ' ')
                body.remove_prefix(1);
            if (body.empty()) {
                if (!pending.empty())
                    pending += "\n\n";
            }
            else {
                if (!pending.empty() && pending.back() != '\n')
                    pending.push_back(' ');
                pending.append(body);
            }
            continue;
        }

        std::string key;
        if (topLevel && startsWith(body, "declare extern type ")) {
            owner = word(body, 20);
        }
        else if (topLevel && startsWith(body, "export type ")) {
            owner = word(body, 12);
        }
        else if (topLevel && startsWith(body, "declare function ")) {
            key = word(body, 17);
            owner.clear();
        }
        else if (topLevel && startsWith(body, "declare ")) {
            owner = word(body, 8);
            key = owner;
        }
        else if (topLevel) {
            owner.clear();
        }
        else if (!owner.empty()) {
            std::string_view member = body;
            if (startsWith(member, "read "))
                member.remove_prefix(5);
            if (startsWith(member, "function "))
                member.remove_prefix(9);
            const std::string name = word(member, 0);
            if (!name.empty())
                key = owner + "." + name;
        }
        if (!key.empty() && !pending.empty())
            docs.emplace(std::move(key), pending);
        pending.clear();
    }
    return docs;
}

// --- Requires, walked through the snapshot ------------------------------------

[[nodiscard]] std::optional<std::string> stringArgument(const Luau::AstExprCall& call)
{
    if (call.args.size != 1)
        return std::nullopt;
    if (const auto* text = call.args.data[0]->as<Luau::AstExprConstantString>(); text != nullptr)
        return std::string(text->value.data, text->value.size);
    return std::nullopt;
}

struct TreeResolver final : Luau::FileResolver
{
    const LanguageTree* tree = nullptr;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override
    {
        const std::optional<u32> at = tree != nullptr ? tree->find(name) : std::nullopt;
        if (!at.has_value() || !tree->nodes[*at].script)
            return std::nullopt;
        const LanguageTree::Node& node = tree->nodes[*at];
        return Luau::SourceCode{node.source, node.module ? Luau::SourceCode::Module : Luau::SourceCode::Script};
    }

    // One step of a require's walk, from where the step before it arrived
    // (`context`). Luau's own tracer does the walking -- through locals too --
    // and asks this for each step.
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* expr,
                                                  const Luau::TypeCheckLimits&) override
    {
        if (tree == nullptr || tree->nodes.empty())
            return std::nullopt;
        const auto named = [this](u32 index) { return Luau::ModuleInfo{tree->nodes[index].path}; };
        const auto childWhere = [this](u32 parent, const auto& test) -> std::optional<u32> {
            for (const u32 child : tree->nodes[parent].children) {
                if (test(tree->nodes[child]))
                    return child;
            }
            return std::nullopt;
        };

        if (const auto* global = expr->as<Luau::AstExprGlobal>(); global != nullptr) {
            const std::string_view name = global->name.value;
            if (name == "game")
                return named(0);
            if (name == "workspace") {
                if (const auto at = childWhere(0, [](const auto& node) { return node.className == "Workspace"; }))
                    return named(*at);
                return std::nullopt;
            }
            if (name == "script" && context != nullptr)
                return Luau::ModuleInfo{context->name};
            return std::nullopt;
        }

        const std::optional<u32> from = context != nullptr ? tree->find(context->name) : std::nullopt;
        if (!from.has_value())
            return std::nullopt;

        if (const auto* index = expr->as<Luau::AstExprIndexName>(); index != nullptr) {
            const std::string_view name = index->index.value;
            if (name == "Parent") {
                const i32 parent = tree->nodes[*from].parent;
                return parent >= 0 ? std::optional<Luau::ModuleInfo>(named(static_cast<u32>(parent))) : std::nullopt;
            }
            if (const auto at = childWhere(*from, [name](const auto& node) { return node.name == name; }))
                return named(*at);
            return std::nullopt;
        }

        if (const auto* call = expr->as<Luau::AstExprCall>(); call != nullptr && call->self) {
            const auto* method = call->func->as<Luau::AstExprIndexName>();
            const std::optional<std::string> argument = stringArgument(*call);
            if (method == nullptr || !argument.has_value())
                return std::nullopt;
            const std::string_view verb = method->index.value;
            std::optional<u32> at;
            if (verb == "GetService" || verb == "FindService")
                at = childWhere(*from, [&](const auto& node) { return node.className == *argument; });
            else if (verb == "WaitForChild" || verb == "FindFirstChild")
                at = childWhere(*from, [&](const auto& node) { return node.name == *argument; });
            return at.has_value() ? std::optional<Luau::ModuleInfo>(named(*at)) : std::nullopt;
        }

        if (const auto* index = expr->as<Luau::AstExprIndexExpr>(); index != nullptr) {
            if (const auto* text = index->index->as<Luau::AstExprConstantString>(); text != nullptr) {
                const std::string name(text->value.data, text->value.size);
                if (const auto at = childWhere(*from, [&](const auto& node) { return node.name == name; }))
                    return named(*at);
            }
        }
        return std::nullopt;
    }

    std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override { return name; }
};

[[nodiscard]] CompletionKind kindOf(const Luau::AutocompleteEntry& entry)
{
    switch (entry.kind) {
    case Luau::AutocompleteEntryKind::Keyword:
        return CompletionKind::Keyword;
    case Luau::AutocompleteEntryKind::Type:
        return CompletionKind::Class;
    case Luau::AutocompleteEntryKind::Module:
        return CompletionKind::Module;
    case Luau::AutocompleteEntryKind::Property:
        if (entry.type.has_value() && Luau::get<Luau::FunctionType>(Luau::follow(*entry.type)) != nullptr)
            return CompletionKind::Method;
        return CompletionKind::Property;
    default:
        return CompletionKind::Identifier;
    }
}

[[nodiscard]] Luau::ToStringOptions shortTypes()
{
    Luau::ToStringOptions options;
    options.exhaustive = false;
    options.maxTableLength = 6;
    options.maxTypeLength = 80;
    options.functionTypeArguments = true;
    options.useLineBreaks = false;
    return options;
}

// Errors the editor already says better, or that are not errors here: a parse
// error is the document's own (`ScriptDocument::diagnostics`), and a require
// the checker cannot follow is a require of something not in the tree yet.
// A property a class does not declare is how a CHILD is reached -- the tree
// knows those (ADR 0078), the definitions cannot.
[[nodiscard]] bool reported(const Luau::TypeError& error)
{
    if (Luau::get<Luau::SyntaxError>(error) != nullptr || Luau::get<Luau::UnknownRequire>(error) != nullptr)
        return false;
    if (const auto* unknown = Luau::get<Luau::UnknownProperty>(error); unknown != nullptr) {
        const Luau::TypeId table = Luau::follow(unknown->table);
        if (Luau::get<Luau::ExternType>(table) != nullptr)
            return false;
        // An instance typed from the tree (`Impl::typeTheTree`) is its class
        // and its known children; a name that is neither is a child the
        // snapshot did not carry, which is still the tree's to know.
        if (const auto* parts = Luau::get<Luau::IntersectionType>(table); parts != nullptr) {
            for (const Luau::TypeId part : parts->parts) {
                if (Luau::get<Luau::ExternType>(Luau::follow(part)) != nullptr)
                    return false;
            }
        }
    }
    return true;
}

// The textual count of commas at the call's own depth, between its `(` and the
// caret: robust to the half-typed argument a parse cannot place. `arguments`
// is the call's `argLocation`, which starts just AFTER the `(`.
[[nodiscard]] u32 activeArgument(std::string_view source, Luau::Location arguments, Luau::Position caret)
{
    u32 line = 0;
    std::size_t at = 0;
    while (line < arguments.begin.line && at < source.size()) {
        if (source[at++] == '\n')
            ++line;
    }
    at += arguments.begin.column;
    u32 column = arguments.begin.column;
    u32 depth = 0;
    u32 commas = 0;
    char quote = 0;
    while (at < source.size() && (line < caret.line || (line == caret.line && column < caret.column))) {
        const char c = source[at++];
        ++column;
        if (c == '\n') {
            ++line;
            column = 0;
            continue;
        }
        if (quote != 0) {
            if (c == '\\')
                ++at, ++column;
            else if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`')
            quote = c;
        else if (c == '(' || c == '{' || c == '[')
            ++depth;
        else if ((c == ')' || c == '}' || c == ']') && depth > 0)
            --depth;
        else if (c == ',' && depth == 0)
            ++commas;
    }
    return commas;
}

// **A type as a person reads it.** `Vector3` is the engine's name for Luau's
// native `vector` (R9), so the checker prints `vector` wherever one appears;
// the editor shows the name a script writes.
[[nodiscard]] std::string readable(std::string text)
{
    static const std::regex native(R"(\bvector\b)");
    // And an inferred generic the way it is written, `a` and not the solver's
    // `'a` -- a quote a script can never type in that place.
    static const std::regex generic(R"('([A-Za-z_]\w*))");
    return std::regex_replace(std::regex_replace(text, native, "Vector3"), generic, "$1");
}

// The members a table type has, its metatable's `__index` included: what
// `x.` reaches on a value of it. Metamethods are left out -- `__index` is how
// a class is built, not something anybody reaches through a dot.
void membersOfType(Luau::TypeId type, Luau::ToStringOptions& types, std::vector<Completion>& out, int depth = 0)
{
    type = Luau::follow(type);
    if (depth > 4)
        return;
    const auto add = [&](const Luau::TableType::Props& props) {
        for (const auto& [name, property] : props) {
            if (name.rfind("__", 0) == 0 || !property.readTy.has_value())
                continue;
            if (std::any_of(out.begin(), out.end(), [&](const Completion& row) { return row.label == name; }))
                continue;
            const bool callable = Luau::get<Luau::FunctionType>(Luau::follow(*property.readTy)) != nullptr;
            out.push_back(Completion{name, readable(Luau::toString(*property.readTy, types)), "",
                                     callable ? CompletionKind::Method : CompletionKind::Property});
        }
    };
    if (const auto* table = Luau::get<Luau::TableType>(type); table != nullptr) {
        add(table->props);
        return;
    }
    if (const auto* meta = Luau::get<Luau::MetatableType>(type); meta != nullptr) {
        membersOfType(meta->table, types, out, depth + 1);
        if (const auto* metatable = Luau::get<Luau::TableType>(Luau::follow(meta->metatable)); metatable != nullptr) {
            if (const auto index = metatable->props.find("__index");
                index != metatable->props.end() && index->second.readTy.has_value())
                membersOfType(*index->second.readTy, types, out, depth + 1);
        }
    }
}

// The value a local was declared with, when it was declared with exactly one.
struct LocalValue : Luau::AstVisitor
{
    const Luau::AstLocal* local = nullptr;
    const Luau::AstExpr* value = nullptr;

    bool visit(Luau::AstStatLocal* node) override
    {
        for (std::size_t index = 0; index < node->vars.size && index < node->values.size; ++index) {
            if (node->vars.data[index] == local)
                value = node->values.data[index];
        }
        return value == nullptr;
    }
};

// The text a location spans in `source`, on one line or several.
[[nodiscard]] std::string textAt(std::string_view source, Luau::Location location)
{
    const auto offsetOf = [source](Luau::Position at) -> std::size_t {
        std::size_t offset = 0;
        for (unsigned line = 0; line < at.line && offset < source.size(); ++offset) {
            if (source[offset] == '\n')
                ++line;
        }
        return std::min(offset + at.column, source.size());
    };
    const std::size_t begin = offsetOf(location.begin);
    const std::size_t end = offsetOf(location.end);
    return end > begin ? std::string(source.substr(begin, end - begin)) : std::string{};
}

// The function expression written at `where`.
struct FunctionAt : Luau::AstVisitor
{
    Luau::Location where;
    const Luau::AstExprFunction* found = nullptr;

    bool visit(Luau::AstExprFunction* node) override
    {
        if (found == nullptr && node->location == where)
            found = node;
        return found == nullptr;
    }
};

// The first `return x` of the function written at `where`, not counting the
// functions inside it.
struct FirstReturn : Luau::AstVisitor
{
    Luau::Location where;
    const Luau::AstExprFunction* function = nullptr;
    const Luau::AstExpr* returned = nullptr;

    bool visit(Luau::AstExprFunction* node) override
    {
        if (function == nullptr && node->location == where) {
            function = node;
            return true;
        }
        return function == nullptr;
    }
    bool visit(Luau::AstStatReturn* node) override
    {
        if (function != nullptr && returned == nullptr && node->list.size > 0)
            returned = node->list.data[0];
        return false;
    }
};

// --- `Instance.new`, by name ---------------------------------------------------

// **What `Instance.new("Part")` is, told to the new solver the way the
// reference editor's checker tells it**: the definitions' forty-odd overloads
// -- `(("Part") -> Part) & (("Model") -> Model) & ...` -- are "code too complex"
// to the new solver, so the checker is given `(className: string) -> Instance`
// and this answers the call instead: a literal name of a creatable class is
// that class, anything else is an `Instance`. The overloads stay in the file,
// which `luaug check` and every other reader still use.
struct CreatableClasses
{
    std::string definitions;
    std::vector<std::pair<std::string, std::string>> classes;
};

[[nodiscard]] CreatableClasses withoutInstanceOverloads(std::string_view definitions)
{
    CreatableClasses out;
    out.definitions = std::string(definitions);
    constexpr std::string_view Head = "declare Instance: {\n    new: ";
    const std::size_t start = out.definitions.find(Head);
    if (start == std::string::npos)
        return out;
    const std::size_t from = start + Head.size();
    const std::size_t end = out.definitions.find(",\n    ", from);
    if (end == std::string::npos)
        return out;
    const std::string_view overloads = std::string_view(out.definitions).substr(from, end - from);
    for (std::size_t at = overloads.find("((\""); at != std::string_view::npos; at = overloads.find("((\"", at + 1)) {
        const std::size_t nameEnd = overloads.find('"', at + 3);
        const std::size_t arrow = overloads.find("-> ", nameEnd);
        const std::size_t close = overloads.find(')', arrow);
        if (nameEnd == std::string_view::npos || arrow == std::string_view::npos || close == std::string_view::npos)
            break;
        out.classes.emplace_back(std::string(overloads.substr(at + 3, nameEnd - at - 3)),
                                 std::string(overloads.substr(arrow + 3, close - arrow - 3)));
    }
    out.definitions.replace(from, end - from, "(className: string) -> Instance");
    return out;
}

class MagicInstanceNew final : public Luau::MagicFunction
{
public:
    std::unordered_map<std::string, Luau::TypeId> classes;

    std::optional<Luau::WithPredicate<Luau::TypePackId>> handleOldSolver(Luau::TypeChecker&,
                                                                         const std::shared_ptr<Luau::Scope>&,
                                                                         const Luau::AstExprCall&,
                                                                         Luau::WithPredicate<Luau::TypePackId>) override
    {
        return std::nullopt;
    }

    bool infer(const Luau::MagicFunctionCallContext& context) override
    {
        if (context.callSite->args.size < 1)
            return false;
        const auto* name = context.callSite->args.data[0]->as<Luau::AstExprConstantString>();
        if (name == nullptr)
            return false;
        const auto found = classes.find(std::string(name->value.data, name->value.size));
        if (found == classes.end())
            return false;
        const Luau::TypePackId result = context.solver->arena->addTypePack({found->second});
        Luau::asMutable(context.result)->ty.emplace<Luau::BoundTypePack>(result);
        return true;
    }
};

} // namespace

struct LanguageCore::Impl
{
    TreeResolver resolver;
    Luau::NullConfigResolver config;
    Luau::Frontend frontend;
    LanguageTree tree;
    std::unordered_map<std::string, std::string> docs;
    std::string loadError;

    // **Every instance of the tree as a type** (the owner: `script.Parent.Music`
    // was "value of type 'Instance?' could be nil" -- the definitions only
    // know a script's parent is some Instance or nothing, and the tree knows
    // exactly which). Each is its class and a table of its `Parent` and its
    // children by name; `script`, `game` and `workspace` are bound to them in
    // every module's own scope, which is how the reference editor types the
    // same code. Rebuilt when the tree's SHAPE changes, never on a keystroke,
    // and the arenas stay alive because a module checked earlier still points
    // into them until it is checked again.
    std::vector<std::unique_ptr<Luau::TypeArena>> treeArenas;
    std::unordered_map<std::string, Luau::TypeId> treeTypes;
    Luau::TypeId gameType = nullptr;
    Luau::TypeId workspaceType = nullptr;
    std::string treeShape;

    static Luau::FrontendOptions options()
    {
        Luau::FrontendOptions out;
        out.retainFullTypeGraphs = true;
        return out;
    }

    explicit Impl(std::string_view definitions)
        // **The new solver** (the owner, 2026-09-25, and R2; ADR 0093 as
        // amended). It had stopped at the definitions' `Instance.new` -- one
        // intersection of forty-odd overloads -- with "code too complex"; it
        // is now given one signature and a magic function that answers a
        // literal class name (`MagicInstanceNew`), which is how the reference
        // editor's checker types the same call. The new solver has one module
        // and one set of globals for checking and completing alike, so there
        // is no second copy of either.
        : frontend(Luau::SolverMode::New, &resolver, &config, options())
    {
        resolver.tree = &tree;
        frontend.prepareModuleScope = [this](const Luau::ModuleName& name, const Luau::ScopePtr& scope, bool) {
            const auto bind = [&scope](const char* global, Luau::TypeId type) {
                Luau::Binding binding;
                binding.typeId = type;
                scope->bindings[Luau::AstName(global)] = binding;
            };
            if (const auto found = treeTypes.find(name); found != treeTypes.end())
                bind("script", found->second);
            if (gameType != nullptr)
                bind("game", gameType);
            if (workspaceType != nullptr)
                bind("workspace", workspaceType);
        };
        Luau::registerBuiltinGlobals(frontend, frontend.globals, false);
        const CreatableClasses creatable = withoutInstanceOverloads(definitions);
        {
            Luau::GlobalTypes& globals = frontend.globals;
            const Luau::LoadDefinitionFileResult loaded = frontend.loadDefinitionFile(
                globals, globals.globalScope, creatable.definitions, "@luaug", /*captureComments*/ false, false);
            if (!loaded.success && loadError.empty()) {
                loadError = "the engine definitions did not load";
                if (!loaded.parseResult.errors.empty())
                    loadError += ": " + loaded.parseResult.errors.front().getMessage();
                else if (loaded.module != nullptr && !loaded.module->errors.empty())
                    loadError += " (line " + std::to_string(loaded.module->errors.front().location.begin.line + 1) +
                                 "): " + Luau::toString(loaded.module->errors.front()) + " -- " +
                                 std::to_string(loaded.module->errors.size()) + " error(s)";
            }
            attachInstanceNew(globals, creatable);
            vectorIsVector3(globals);
            Luau::freeze(globals.globalTypes);
        }
        docs = indexDocs(definitions);
    }

    // **A `vector` is a `Vector3` to the checker** (the owner: "some values
    // are still `vector` rather than `Vector3`" -- `size.X` was an error). A
    // part's `Size` is the VM's native vector, whose Luau type declares only
    // `x`, `y` and `z`; the definitions describe everything else a Vector3 has
    // on a type of its own name, which no value ever has. So the members are
    // copied onto the native type: `X`, `Y` and `Z` beside the lowercase ones --
    // the interpreter answers either spelling -- and `Magnitude`, `Unit` and
    // the methods, each method's `self` retyped from `Vector3` to `vector`.
    static void vectorIsVector3(Luau::GlobalTypes& globals)
    {
        const auto native = globals.globalScope->exportedTypeBindings.find("vector");
        const std::optional<Luau::TypeFun> declared = globals.globalScope->lookupType("Vector3");
        if (native == globals.globalScope->exportedTypeBindings.end() || !declared.has_value())
            return;
        const Luau::TypeId vectorType = native->second.type;
        auto* vector = Luau::getMutable<Luau::ExternType>(vectorType);
        const auto* vector3 = Luau::get<Luau::ExternType>(Luau::follow(declared->type));
        if (vector == nullptr || vector3 == nullptr)
            return;
        const Luau::TypeId vector3Type = Luau::follow(declared->type);
        Luau::TypeArena& arena = globals.globalTypes;

        // A method's `self`, and any other `Vector3` in its signature, is the
        // native vector the value really is.
        const auto retyped = [&](Luau::TypeId type) {
            const auto* function = Luau::get<Luau::FunctionType>(Luau::follow(type));
            if (function == nullptr)
                return type;
            const auto swap = [&](Luau::TypePackId pack) {
                auto [types, tail] = Luau::flatten(pack);
                for (Luau::TypeId& each : types) {
                    if (Luau::follow(each) == vector3Type)
                        each = vectorType;
                }
                return arena.addTypePack(Luau::TypePack{std::move(types), tail});
            };
            Luau::FunctionType copy = *function;
            copy.argTypes = swap(function->argTypes);
            copy.retTypes = swap(function->retTypes);
            return arena.addType(std::move(copy));
        };
        for (const auto& [name, property] : vector3->props) {
            if (vector->props.count(name) != 0 || !property.readTy.has_value())
                continue;
            Luau::Property copy = property;
            copy.readTy = retyped(*property.readTy);
            if (copy.writeTy.has_value())
                copy.writeTy = copy.readTy;
            vector->props[name] = copy;
        }
        for (const auto& [lower, upper] : {std::pair<const char*, const char*>{"x", "X"}, {"y", "Y"}, {"z", "Z"}}) {
            const auto found = vector->props.find(lower);
            if (found != vector->props.end() && vector->props.count(upper) == 0)
                vector->props[upper] = found->second;
        }
    }

    // The tree's shape: what a type built from it depends on. The sources are
    // not in it -- a keystroke does not rebuild the types.
    [[nodiscard]] static std::string shapeOf(const LanguageTree& snapshot)
    {
        std::string shape;
        for (const LanguageTree::Node& node : snapshot.nodes) {
            shape += node.path;
            shape += '\x1f';
            shape += node.className;
            shape += '\x1e';
        }
        return shape;
    }

    // Builds `treeTypes`, `gameType` and `workspaceType` from `tree` (see
    // `treeArenas`). Answers whether it rebuilt, which makes every module
    // dirty: each one's `script` and `game` are new types.
    bool typeTheTree()
    {
        std::string shape = shapeOf(tree);
        if (shape == treeShape)
            return false;
        treeShape = std::move(shape);
        treeTypes.clear();
        gameType = nullptr;
        workspaceType = nullptr;

        auto arena = std::make_unique<Luau::TypeArena>();
        const Luau::ScopePtr& globalsScope = frontend.globals.globalScope;
        const Luau::TypeId instanceType = [&]() -> Luau::TypeId {
            const std::optional<Luau::TypeFun> declared = globalsScope->lookupType("Instance");
            return declared.has_value() ? declared->type : frontend.builtinTypes->anyType;
        }();
        // A member of the class, anywhere up its chain: a child of the same
        // name is shadowed by it, as it is when the code runs.
        const auto classHas = [](Luau::TypeId classType, const std::string& member) {
            for (const Luau::ExternType* at = Luau::get<Luau::ExternType>(Luau::follow(classType)); at != nullptr;
                 at = at->parent.has_value() ? Luau::get<Luau::ExternType>(Luau::follow(*at->parent)) : nullptr) {
                if (at->props.count(member) != 0)
                    return true;
            }
            return false;
        };

        const std::size_t count = tree.nodes.size();
        std::vector<Luau::TypeId> tables(count);
        std::vector<Luau::TypeId> classes(count);
        std::vector<Luau::TypeId> nodes(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::optional<Luau::TypeFun> declared = globalsScope->lookupType(tree.nodes[index].className);
            classes[index] = declared.has_value() ? declared->type : instanceType;
            tables[index] = arena->addType(Luau::TableType{Luau::TableState::Sealed, Luau::TypeLevel{}});
            nodes[index] = arena->addType(Luau::IntersectionType{{classes[index], tables[index]}});
        }
        for (std::size_t index = 0; index < count; ++index) {
            const LanguageTree::Node& node = tree.nodes[index];
            auto* table = Luau::getMutable<Luau::TableType>(tables[index]);
            if (node.parent >= 0)
                table->props["Parent"] =
                    Luau::Property::create(nodes[static_cast<std::size_t>(node.parent)], std::nullopt);
            for (const core::u32 child : node.children) {
                const std::string& name = tree.nodes[child].name;
                if (name.empty() || table->props.count(name) != 0 || classHas(classes[index], name))
                    continue;
                table->props[name] = Luau::Property::create(nodes[child], std::nullopt);
            }
            if (node.script)
                treeTypes.emplace(node.path, nodes[index]);
            if (node.parent < 0)
                gameType = nodes[index];
            else if (node.parent == 0 && node.className == "Workspace")
                workspaceType = nodes[index];
        }
        Luau::freeze(*arena);
        treeArenas.push_back(std::move(arena));
        return true;
    }

    // Hands `Instance.new` its magic (see `MagicInstanceNew`): each creatable
    // name to the type the definitions declare for it.
    static void attachInstanceNew(Luau::GlobalTypes& globals, const CreatableClasses& creatable)
    {
        const std::optional<Luau::Binding> instance = globals.globalScope->linearSearchForBinding("Instance");
        if (!instance.has_value())
            return;
        const auto* table = Luau::get<Luau::TableType>(Luau::follow(instance->typeId));
        if (table == nullptr)
            return;
        const auto newProperty = table->props.find("new");
        if (newProperty == table->props.end() || !newProperty->second.readTy.has_value())
            return;
        auto magic = std::make_shared<MagicInstanceNew>();
        for (const auto& [name, className] : creatable.classes) {
            if (const std::optional<Luau::TypeFun> declared = globals.globalScope->lookupType(className);
                declared.has_value())
                magic->classes.emplace(name, declared->type);
        }
        Luau::attachMagicFunction(Luau::follow(*newProperty->second.readTy), std::move(magic));
    }

    // The one checked module the new solver keeps, for checking and
    // completing alike.
    [[nodiscard]] Luau::ModulePtr checked(const std::string& module, bool forAutocomplete)
    {
        (void)forAutocomplete;
        Luau::FrontendOptions options = Impl::options();
        (void)frontend.check(module, options);
        return frontend.moduleResolver.getModule(module);
    }

    // **What a constructor BUILT, when what it was declared to return is an
    // error** -- the owner's snake: `function Snake.new(...): Snake` names a
    // type the module never declares, so `local snake = Snake.new()` is an
    // error type to the checker and `snake.` had nothing to offer. The
    // function's own `return` still has the type of what it made.
    [[nodiscard]] std::optional<Luau::TypeId> builtBy(const std::string& module, const Luau::ModulePtr& checked,
                                                      const Luau::AstExpr* subject)
    {
        const Luau::TypeId* declared = checked->astTypes.find(subject);
        if (declared != nullptr && Luau::get<Luau::ErrorType>(Luau::follow(*declared)) == nullptr)
            return std::nullopt;
        const auto* local = subject->as<Luau::AstExprLocal>();
        const Luau::SourceModule* source = frontend.getSourceModule(module);
        if (local == nullptr || source == nullptr || source->root == nullptr)
            return std::nullopt;
        LocalValue value;
        value.local = local->local;
        source->root->visit(&value);
        const auto* call = value.value != nullptr ? value.value->as<Luau::AstExprCall>() : nullptr;
        if (call == nullptr)
            return std::nullopt;
        const Luau::TypeId* callee = checked->astTypes.find(call->func);
        const auto* function = callee != nullptr ? Luau::get<Luau::FunctionType>(Luau::follow(*callee)) : nullptr;
        if (function == nullptr || !function->definition.has_value() ||
            !function->definition->definitionModuleName.has_value())
            return std::nullopt;
        const std::string& owner = *function->definition->definitionModuleName;
        const Luau::ModulePtr defined = frontend.moduleResolver.getModule(owner);
        const Luau::SourceModule* definedSource = frontend.getSourceModule(owner);
        if (defined == nullptr || definedSource == nullptr || definedSource->root == nullptr)
            return std::nullopt;
        FirstReturn first;
        first.where = function->definition->definitionLocation;
        definedSource->root->visit(&first);
        if (first.returned == nullptr)
            return std::nullopt;
        const Luau::TypeId* built = defined->astTypes.find(first.returned);
        return built != nullptr ? std::optional<Luau::TypeId>(*built) : std::nullopt;
    }

    // **A parameter's or a result's type as it was WRITTEN, when the checker
    // could not resolve it**: `gridPos: Position` in a module that never
    // declares `Position` is an error type, and `*error-type*` in a signature
    // tells nobody anything. The annotation's own text is what they wrote,
    // and the underline on it says why it did not resolve.
    [[nodiscard]] const Luau::AstExprFunction* definitionOf(const Luau::FunctionType& function, std::string& source)
    {
        if (!function.definition.has_value() || !function.definition->definitionModuleName.has_value())
            return nullptr;
        const std::string& owner = *function.definition->definitionModuleName;
        const std::optional<u32> node = tree.find(owner);
        const Luau::SourceModule* module = frontend.getSourceModule(owner);
        if (!node.has_value() || module == nullptr || module->root == nullptr)
            return nullptr;
        FunctionAt at;
        at.where = function.definition->definitionLocation;
        module->root->visit(&at);
        source = tree.nodes[*node].source;
        return at.found;
    }

    [[nodiscard]] std::string docFor(const Luau::ModulePtr& module, const Luau::AstExpr* callee) const
    {
        const auto lookup = [this](const std::string& key) {
            const auto found = docs.find(key);
            return found != docs.end() ? found->second : std::string{};
        };
        if (const auto* global = callee->as<Luau::AstExprGlobal>(); global != nullptr)
            return lookup(global->name.value);
        const auto* index = callee->as<Luau::AstExprIndexName>();
        if (index == nullptr)
            return {};
        const std::string member = index->index.value;
        if (const auto* base = index->expr->as<Luau::AstExprGlobal>(); base != nullptr) {
            if (std::string doc = lookup(std::string(base->name.value) + "." + member); !doc.empty())
                return doc;
        }
        if (module == nullptr)
            return {};
        if (const Luau::TypeId* type = module->astTypes.find(index->expr); type != nullptr) {
            // Up the extern type's parents: `part:Destroy()` is `Instance`'s.
            for (const Luau::ExternType* owner = Luau::get<Luau::ExternType>(Luau::follow(*type)); owner != nullptr;
                 owner = owner->parent.has_value() ? Luau::get<Luau::ExternType>(Luau::follow(*owner->parent))
                                                   : nullptr) {
                if (std::string doc = lookup(owner->name + "." + member); !doc.empty())
                    return doc;
            }
        }
        return {};
    }
};

LanguageCore::LanguageCore(std::string_view definitions) : m_impl(std::make_unique<Impl>(definitions))
{}

LanguageCore::~LanguageCore() = default;

const std::string& LanguageCore::loadError() const noexcept
{
    return m_impl->loadError;
}

void LanguageCore::update(LanguageTree tree)
{
    // Dirty what changed and what left: the frontend re-checks their
    // dependents on its own.
    std::map<std::string, const std::string*> before;
    for (const LanguageTree::Node& node : m_impl->tree.nodes) {
        if (node.script)
            before.emplace(node.path, &node.source);
    }
    std::vector<std::string> dirty;
    for (const LanguageTree::Node& node : tree.nodes) {
        if (!node.script)
            continue;
        const auto was = before.find(node.path);
        if (was == before.end() || *was->second != node.source)
            dirty.push_back(node.path);
        if (was != before.end())
            before.erase(was);
    }
    for (const auto& [path, source] : before)
        dirty.push_back(path);

    m_impl->tree = std::move(tree);
    if (m_impl->typeTheTree()) {
        dirty.clear();
        for (const LanguageTree::Node& node : m_impl->tree.nodes) {
            if (node.script)
                dirty.push_back(node.path);
        }
    }
    for (const std::string& path : dirty)
        m_impl->frontend.markDirty(path);
}

LanguageCheck LanguageCore::check(const std::string& module)
{
    LanguageCheck out;
    Luau::FrontendOptions options = Impl::options();
    // **Luau's own linter too** (the owner: "it should not let me define the
    // same thing twice" -- a table type's field written twice is the
    // `TableLiteral` lint, not a type error). The set is the reference
    // editor's: the defaults, less the three unused-name lints it turns off
    // and the unknown global the checker already reports -- which is also
    // what keeps them from repeating this editor's own lint of the same.
    options.runLintChecks = true;
    Luau::LintOptions lints;
    lints.setDefaults();
    for (const Luau::LintWarning::Code off :
         {Luau::LintWarning::Code_UnknownGlobal, Luau::LintWarning::Code_LocalUnused,
          Luau::LintWarning::Code_FunctionUnused, Luau::LintWarning::Code_ImportUnused})
        lints.disableWarning(off);
    options.enabledLintWarnings = lints;
    const Luau::CheckResult result = m_impl->frontend.check(module, options);
    for (const bool asError : {true, false}) {
        for (const Luau::LintWarning& lint : asError ? result.lintResult.errors : result.lintResult.warnings) {
            Diagnostic diagnostic;
            diagnostic.at = Position{lint.location.begin.line, lint.location.begin.column};
            diagnostic.length = lint.location.end.line == lint.location.begin.line
                                    ? lint.location.end.column - lint.location.begin.column
                                    : 0;
            diagnostic.message = lint.text;
            diagnostic.severity = asError ? Severity::Error : Severity::Warning;
            out.diagnostics.push_back(std::move(diagnostic));
        }
    }
    for (const Luau::TypeError& error : result.errors) {
        if (error.moduleName != module || !reported(error))
            continue;
        Diagnostic diagnostic;
        diagnostic.at = Position{error.location.begin.line, error.location.begin.column};
        diagnostic.length = error.location.end.line == error.location.begin.line
                                ? error.location.end.column - error.location.begin.column
                                : 0;
        diagnostic.message = Luau::toString(error);
        // **Luau's message says what is wrong and not what to write** (the
        // owner, on `Signal.new<<Vector2>>()`): a function generic over a PACK
        // takes its explicit arguments as a pack, which is written in
        // parentheses. One type where only packs are wanted is that mistake.
        if (const auto* count = Luau::get<Luau::TypeInstantiationCountMismatch>(error);
            count != nullptr && count->maximumTypes == 0 && count->providedTypePacks == 0 &&
            count->maximumTypePacks > 0 && count->providedTypes <= count->maximumTypePacks)
            diagnostic.message += " It takes a type pack: write the types in parentheses, as in <<(Vector2)>>.";
        diagnostic.severity = Severity::Error;
        out.diagnostics.push_back(std::move(diagnostic));
    }
    return out;
}

LanguageCompletions LanguageCore::complete(const std::string& module, Position at)
{
    LanguageCompletions answer;
    std::vector<Completion>& out = answer.items;
    if (!m_impl->tree.find(module).has_value())
        return answer;
    const Luau::ModulePtr checked = m_impl->checked(module, true);
    if (checked == nullptr)
        return answer;
    const Luau::AutocompleteResult result = Luau::autocomplete(
        m_impl->frontend, module, Luau::Position{at.line, at.column},
        [](std::string, std::optional<const Luau::ExternType*>,
           std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> { return std::nullopt; });
    answer.inType = result.context == Luau::AutocompleteContext::Type;

    // The owner's name for a member's doc, when the completion is off one.
    std::string owner;
    if (!result.ancestry.empty()) {
        if (const auto* index = result.ancestry.back()->as<Luau::AstExprIndexName>(); index != nullptr) {
            if (const auto* base = index->expr->as<Luau::AstExprGlobal>(); base != nullptr)
                owner = base->name.value;
        }
    }

    Luau::ToStringOptions types = shortTypes();
    for (const auto& [name, entry] : result.entryMap) {
        if (entry.wrongIndexType || entry.kind == Luau::AutocompleteEntryKind::GeneratedFunction ||
            entry.kind == Luau::AutocompleteEntryKind::RequirePath || entry.kind == Luau::AutocompleteEntryKind::String)
            continue;
        // Metamethods: how a class is built, not what anybody reaches for.
        if (name.rfind("__", 0) == 0)
            continue;
        Completion completion;
        completion.label = name;
        completion.kind = kindOf(entry);
        if (entry.type.has_value())
            completion.detail = readable(Luau::toString(*entry.type, types));
        else if (entry.kind == Luau::AutocompleteEntryKind::Keyword)
            completion.detail = "keyword";
        const auto doc = [this](const std::string& key) {
            const auto found = m_impl->docs.find(key);
            return found != m_impl->docs.end() ? found->second : std::string{};
        };
        if (entry.containingExternType.has_value()) {
            for (const Luau::ExternType* type = *entry.containingExternType; type != nullptr && completion.doc.empty();
                 type = type->parent.has_value() ? Luau::get<Luau::ExternType>(Luau::follow(*type->parent)) : nullptr)
                completion.doc = doc(type->name + "." + name);
        }
        else if (!owner.empty()) {
            completion.doc = doc(owner + "." + name);
        }
        else {
            completion.doc = doc(name);
        }
        out.push_back(std::move(completion));
    }
    // Nothing, off a value whose declared type is an error: what the function
    // that made it returned (see `builtBy`).
    if (out.empty() && !answer.inType && !result.ancestry.empty()) {
        if (const auto* index = result.ancestry.back()->as<Luau::AstExprIndexName>(); index != nullptr) {
            if (const std::optional<Luau::TypeId> built = m_impl->builtBy(module, checked, index->expr))
                membersOfType(*built, types, out);
        }
    }
    std::sort(out.begin(), out.end(), [](const Completion& a, const Completion& b) { return a.label < b.label; });
    return answer;
}

std::optional<SignatureHelp> LanguageCore::signature(const std::string& module, Position at)
{
    const std::optional<u32> node = m_impl->tree.find(module);
    if (!node.has_value())
        return std::nullopt;
    const Luau::ModulePtr checked = m_impl->checked(module, true);
    const Luau::SourceModule* source = m_impl->frontend.getSourceModule(module);
    if (checked == nullptr || source == nullptr)
        return std::nullopt;

    const Luau::Position caret{at.line, at.column};
    const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*source, caret);
    const Luau::AstExprCall* call = nullptr;
    for (auto walk = ancestry.rbegin(); walk != ancestry.rend(); ++walk) {
        // **A function literal is a boundary**: inside the body of a handler
        // passed to `Connect`, the caret is in that call's arguments only
        // technically, and a box about `Connect` over the body is in the way.
        if ((*walk)->is<Luau::AstExprFunction>())
            return std::nullopt;
        if (const auto* candidate = (*walk)->as<Luau::AstExprCall>(); candidate != nullptr) {
            // Inside its parentheses, not on the callee's name. `argLocation`
            // begins just after the `(`, so the caret sitting right there --
            // the moment the call is typed -- is inside; it ends after the
            // `)`, and the caret past that is not.
            if (!(caret < candidate->argLocation.begin) && caret < candidate->argLocation.end) {
                call = candidate;
                break;
            }
        }
    }
    if (call == nullptr)
        return std::nullopt;
    const Luau::TypeId* found = checked->astTypes.find(call->func);
    if (found == nullptr)
        return std::nullopt;
    // **A global function is shown as it is DECLARED**: the type at the call
    // is an instance of it, and a half-typed argument list makes a generic's
    // instance an error -- `print(*error-type*)` rather than `print(...: T)`.
    Luau::TypeId declared = *found;
    if (const auto* global = call->func->as<Luau::AstExprGlobal>(); global != nullptr) {
        if (const std::optional<Luau::Binding> binding =
                m_impl->frontend.globals.globalScope->linearSearchForBinding(global->name.value))
            declared = binding->typeId;
    }
    const Luau::TypeId* callee = &declared;

    // An overloaded constructor is an intersection: the first overload whose
    // arity reaches the argument being typed, or the first.
    const u32 active = activeArgument(m_impl->tree.nodes[*node].source, call->argLocation, caret);
    const Luau::FunctionType* function = nullptr;
    const Luau::TypeId followed = Luau::follow(*callee);
    if (const auto* overloads = Luau::get<Luau::IntersectionType>(followed); overloads != nullptr) {
        for (const Luau::TypeId part : overloads->parts) {
            const auto* candidate = Luau::get<Luau::FunctionType>(Luau::follow(part));
            if (candidate == nullptr)
                continue;
            if (function == nullptr)
                function = candidate;
            const auto [params, tail] = Luau::flatten(candidate->argTypes);
            if (params.size() > active + (call->self ? 1u : 0u) || tail.has_value()) {
                function = candidate;
                break;
            }
        }
    }
    else {
        function = Luau::get<Luau::FunctionType>(followed);
    }
    if (function == nullptr)
        return std::nullopt;

    SignatureHelp help;
    if (const auto* index = call->func->as<Luau::AstExprIndexName>(); index != nullptr)
        help.label = index->index.value;
    else if (const auto* global = call->func->as<Luau::AstExprGlobal>(); global != nullptr)
        help.label = global->name.value;
    else if (const auto* local = call->func->as<Luau::AstExprLocal>(); local != nullptr)
        help.label = local->local->name.value;
    else
        help.label = "function";
    help.label += "(";

    Luau::ToStringOptions types = shortTypes();
    const auto [params, tail] = Luau::flatten(function->argTypes);
    std::string definedIn;
    const Luau::AstExprFunction* written = m_impl->definitionOf(*function, definedIn);
    // The written function's `args` leave out a `self` the type has.
    const std::size_t selfShift = written != nullptr && written->self != nullptr ? 1 : 0;
    // **A parameter that was annotated is shown as it was written**: the
    // name somebody chose for a type says more than what the solver made of
    // it -- an undeclared `Position` is an error type, and an unannotated one
    // is a generic `a` under the new solver.
    const auto shown = [&](Luau::TypeId type, const Luau::AstType* annotation) {
        if (annotation != nullptr)
            return textAt(definedIn, annotation->location);
        return readable(Luau::toString(type, types));
    };
    // A method's `self` is the object before the colon, not an argument.
    const std::size_t first = call->self && !params.empty() ? 1 : 0;
    for (std::size_t index = first; index < params.size(); ++index) {
        if (index > first)
            help.label += ", ";
        const u32 begin = static_cast<u32>(help.label.size());
        if (index < function->argNames.size() && function->argNames[index].has_value())
            help.label += function->argNames[index]->name + ": ";
        const Luau::AstType* annotation = nullptr;
        if (written != nullptr && index >= selfShift && index - selfShift < written->args.size)
            annotation = written->args.data[index - selfShift]->annotation;
        help.label += shown(params[index], annotation);
        help.parameters.emplace_back(begin, static_cast<u32>(help.label.size()));
    }
    // The new solver gives a function declared without `...` a variadic
    // `...any` tail; the signature says what was written.
    const bool variadic = written == nullptr || written->vararg;
    if (tail.has_value() && variadic) {
        if (params.size() > first)
            help.label += ", ";
        const u32 begin = static_cast<u32>(help.label.size());
        // `...: any`, the way a variadic is written in a signature: the pack's
        // own spelling (`...any`, `T...`) without its dots.
        std::string pack = readable(Luau::toString(*tail, types));
        if (pack.rfind("...", 0) == 0)
            pack.erase(0, 3);
        if (pack.size() > 3 && pack.compare(pack.size() - 3, 3, "...") == 0)
            pack.erase(pack.size() - 3);
        help.label += "...: " + pack;
        help.parameters.emplace_back(begin, static_cast<u32>(help.label.size()));
    }
    help.label += ")";
    std::string returns = readable(Luau::toString(function->retTypes, types));
    if (written != nullptr && written->returnAnnotation != nullptr)
        returns = textAt(definedIn, written->returnAnnotation->location);
    if (!returns.empty() && returns != "()")
        help.label += ": " + returns;

    help.active = help.parameters.empty() ? 0 : std::min<u32>(active, static_cast<u32>(help.parameters.size() - 1));
    help.doc = m_impl->docFor(checked, call->func);
    return help;
}

// --- The worker ----------------------------------------------------------------

LanguageService::LanguageService(std::string definitions)
    : m_definitions(std::move(definitions)), m_worker([this]() { run(); })
{}

LanguageService::~LanguageService()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void LanguageService::requestCheck(LanguageTree tree, std::string module, u64 revision)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[static_cast<int>(Kind::Check)] = Request{std::move(tree), std::move(module), Position{}, revision};
    }
    m_wake.notify_all();
}

void LanguageService::requestCompletion(LanguageTree tree, std::string module, Position at, u64 revision)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[static_cast<int>(Kind::Completion)] = Request{std::move(tree), std::move(module), at, revision};
    }
    m_wake.notify_all();
}

void LanguageService::requestSignature(LanguageTree tree, std::string module, Position at, u64 revision)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[static_cast<int>(Kind::Signature)] = Request{std::move(tree), std::move(module), at, revision};
    }
    m_wake.notify_all();
}

std::optional<LanguageService::CheckAnswer> LanguageService::takeCheck()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_check, std::nullopt);
}

std::optional<LanguageService::CompletionAnswer> LanguageService::takeCompletion()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_completion, std::nullopt);
}

std::optional<LanguageService::SignatureAnswer> LanguageService::takeSignature()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_signature, std::nullopt);
}

void LanguageService::run()
{
    // Built here rather than in the constructor -- loading the definitions is
    // the slowest thing the service does, and no frame should wait on it --
    // and at once rather than at the first request, so the first popup does
    // not wait on it either.
    const std::unique_ptr<LanguageCore> core = std::make_unique<LanguageCore>(m_definitions);
    for (;;) {
        Request request;
        Kind kind = Kind::Check;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this]() {
                return m_stopping || m_pending[0].has_value() || m_pending[1].has_value() || m_pending[2].has_value();
            });
            if (m_stopping)
                return;
            // What the person is waiting on first: the popup, then the
            // signature, then the underlines.
            for (const Kind candidate : {Kind::Completion, Kind::Signature, Kind::Check}) {
                std::optional<Request>& slot = m_pending[static_cast<int>(candidate)];
                if (slot.has_value()) {
                    request = std::move(*slot);
                    slot.reset();
                    kind = candidate;
                    break;
                }
            }
        }
        core->update(std::move(request.tree));

        switch (kind) {
        case Kind::Check: {
            CheckAnswer answer;
            answer.module = request.module;
            answer.revision = request.revision;
            answer.check = core->check(request.module);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_check = std::move(answer);
            break;
        }
        case Kind::Completion: {
            CompletionAnswer answer;
            answer.module = request.module;
            answer.revision = request.revision;
            answer.at = request.at;
            answer.completions = core->complete(request.module, request.at);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completion = std::move(answer);
            break;
        }
        case Kind::Signature: {
            SignatureAnswer answer;
            answer.module = request.module;
            answer.revision = request.revision;
            answer.at = request.at;
            answer.signature = core->signature(request.module, request.at);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_signature = std::move(answer);
            break;
        }
        }
    }
}

} // namespace luaug::app
