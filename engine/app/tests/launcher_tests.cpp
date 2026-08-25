// The project browser's model (ADR 0055).
//
// **The launcher is the one shell nothing can photograph**, like the two before
// it: the ImGui half cannot render headlessly and SDL does not accept injected
// input. So everything that is not a pixel is here — which list a person sees,
// in what order, what happens to a project that has been moved, and what the
// Create button actually writes.
//
// The last case is the one worth reading twice. There are two scaffolders in
// this repository now, `luaug new` and this, and the argument for that is in
// ADR 0055: a CLI that needed a window to make a project would be a worse CLI.
// What makes two safe is not care, it is the comparison at the bottom of this
// file.
#include "luaug/app/launcher.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace luaug;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A directory this test owns, removed with it. Real files rather than a fake
// filesystem, because what is under test IS filesystem behaviour: a list that
// survives a process, and a directory copy.
struct Scratch
{
    std::filesystem::path root;

    explicit Scratch(const std::string& name) : root(std::filesystem::temp_directory_path() / "luaug-launcher" / name)
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        REQUIRE(platform::createDirectories(root));
    }

    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    // A directory the engine would open: a `luaug.toml` is enough.
    [[nodiscard]] std::filesystem::path project(const std::string& name, const std::string& declared = {}) const
    {
        const std::filesystem::path directory = root / name;
        REQUIRE(platform::createDirectories(directory));
        const std::string text = declared.empty() ? "[project]\n" : "[project]\nname = \"" + declared + "\"\n";
        REQUIRE(platform::writeTextFile(directory / "luaug.toml", text));
        return directory;
    }
};

// Where the repository keeps the template, found from this test's own file so
// the suite runs from whatever working directory ctest picks.
[[nodiscard]] std::filesystem::path repositoryRoot()
{
    return std::filesystem::path(LUAUG_TEST_CATALOG).parent_path().parent_path();
}

// Every file under `root`, as (relative path, contents), sorted. What the
// comparison at the bottom of this file compares.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> treeOf(const std::filesystem::path& root)
{
    std::vector<std::pair<std::string, std::string>> files;
    std::error_code ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        std::string text;
        if (!platform::readTextFile(entry.path(), text))
            continue;
        files.emplace_back(entry.path().lexically_relative(root).generic_string(), std::move(text));
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

TEST_CASE("a directory is a project when the engine could open it")
{
    Scratch scratch("isproject");

    CHECK(app::isProjectDirectory(scratch.project("withtoml")));

    // The other half of the same question, and the CLI asks both: a project
    // does not need a `luaug.toml`, it needs entry scripts.
    const std::filesystem::path scripts = scratch.root / "withscripts";
    REQUIRE(platform::createDirectories(scripts / "src" / "scripts"));
    CHECK(app::isProjectDirectory(scripts));

    const std::filesystem::path bare = scratch.root / "bare";
    REQUIRE(platform::createDirectories(bare));
    CHECK_FALSE(app::isProjectDirectory(bare));
    CHECK_FALSE(app::isProjectDirectory(scratch.root / "nothing-here"));
    CHECK_FALSE(app::isProjectDirectory({}));
}

TEST_CASE("a project's name is what it calls itself, or its folder")
{
    Scratch scratch("names");
    CHECK(app::projectNameOf(scratch.project("folder", "My Game")) == "My Game");
    // The fallback is the directory, which is what `luaug build` picks too.
    CHECK(app::projectNameOf(scratch.project("unnamed")) == "unnamed");
}

TEST_CASE("the list is most recent first and remembers no duplicates")
{
    Scratch scratch("order");
    const std::filesystem::path a = scratch.project("alpha");
    const std::filesystem::path b = scratch.project("bravo");

    app::ProjectList list;
    list.load(scratch.root / "projects.json");
    list.remember(a);
    list.remember(b);

    REQUIRE(list.entries().size() == 2);
    CHECK(list.entries()[0].path == std::filesystem::weakly_canonical(b));

    // Opening one again moves it, and does not add a row. **Order is the file's
    // order** — there is no timestamp anywhere in this, because the only
    // ordering question a recent list has is which was opened last.
    list.remember(a);
    REQUIRE(list.entries().size() == 2);
    CHECK(list.entries()[0].path == std::filesystem::weakly_canonical(a));

    // And a different spelling of the same path is the same project. Without
    // this, opening through a relative path once produces a second row for a
    // project somebody already has.
    list.remember(scratch.root / "." / "alpha");
    CHECK(list.entries().size() == 2);
}

TEST_CASE("the list survives a process")
{
    Scratch scratch("persist");
    const std::filesystem::path file = scratch.root / "state" / "projects.json";
    const std::filesystem::path project = scratch.project("game", "Persisted");

    {
        app::ProjectList list;
        list.load(file);
        list.remember(project);
        REQUIRE(list.save());
    }

    app::ProjectList reloaded;
    reloaded.load(file);
    REQUIRE(reloaded.entries().size() == 1);
    CHECK(reloaded.entries()[0].path == std::filesystem::weakly_canonical(project));
    // The NAME is read from the project rather than stored, so renaming a game
    // shows the new name without anything having to notice.
    CHECK(reloaded.entries()[0].name == "Persisted");
    CHECK_FALSE(reloaded.entries()[0].missing);
}

TEST_CASE("a first launch is an empty list rather than an error")
{
    Scratch scratch("firstrun");
    app::ProjectList list;
    list.load(scratch.root / "never-written.json");
    CHECK(list.entries().empty());

    // And a file that is not ours is not silently adopted.
    const std::filesystem::path foreign = scratch.root / "foreign.json";
    REQUIRE(platform::writeTextFile(foreign, R"({"format":"something-else","projects":[{"path":"/tmp/x"}]})"));
    list.load(foreign);
    CHECK(list.entries().empty());
}

TEST_CASE("a project that moved stays in the list, marked and removable")
{
    Scratch scratch("missing");
    const std::filesystem::path project = scratch.project("gone", "Gone");

    app::ProjectList list;
    list.load(scratch.root / "projects.json");
    list.remember(project);
    REQUIRE(list.save());

    std::error_code ec;
    std::filesystem::remove_all(project, ec);

    // **Not dropped.** A list that edits itself when a drive is unplugged is a
    // list somebody cannot trust, and "where did my project go" is worse than a
    // row that says so.
    app::ProjectList reloaded;
    reloaded.load(list.file());
    REQUIRE(reloaded.entries().size() == 1);
    CHECK(reloaded.entries()[0].missing);
    CHECK(reloaded.entries()[0].name == "gone");

    reloaded.forget(project);
    CHECK(reloaded.entries().empty());

    // **And it stays forgotten**, which is the half this case stopped one line
    // short of and which is the half a person notices: the list was only ever
    // written when a project was OPENED, so Remove worked until you closed the
    // launcher and then every row came back.
    //
    // Asked THROUGH the drain the loop runs rather than by calling `save`, which
    // is the whole point of that function existing: `save` already worked, and
    // what was missing was anybody calling it. A test that called it itself
    // would pass against the defect.
    app::LauncherView view;
    view.forgot = true;
    CHECK(app::applyProjectDecisions(reloaded, view));
    CHECK_FALSE(view.forgot);

    app::ProjectList afterwards;
    afterwards.load(list.file());
    CHECK(afterwards.entries().empty());

    // Nothing decided, nothing written.
    CHECK_FALSE(app::applyProjectDecisions(reloaded, view));
}

TEST_CASE("a name a project cannot have is refused before anything is written")
{
    seedCatalog();
    Scratch scratch("badname");
    const std::filesystem::path templates = repositoryRoot() / "templates";

    for (const std::string& name : {std::string(""), std::string("my game"), std::string("../escape"),
                                    std::string("a/b"), std::string("what?")}) {
        CHECK_FALSE(app::validProjectName(name));
        const app::NewProjectResult result =
            app::createProject(templates, {}, {.parent = scratch.root, .name = name, .templateName = "starter"});
        CHECK(result.error.has_value());
    }

    CHECK(app::validProjectName("my-game_2"));
}

TEST_CASE("creating over something that exists is refused rather than merged")
{
    seedCatalog();
    Scratch scratch("exists");
    const std::filesystem::path templates = repositoryRoot() / "templates";
    (void)scratch.project("taken");

    const app::NewProjectResult result =
        app::createProject(templates, {}, {.parent = scratch.root, .name = "taken", .templateName = "starter"});
    REQUIRE(result.error.has_value());
    // The one mistake here that costs work, so it is a refusal and not a merge.
    CHECK(platform::fileExists(scratch.root / "taken" / "luaug.toml"));
    CHECK_FALSE(platform::fileExists(scratch.root / "taken" / "src" / "scripts" / "main.luau"));
}

TEST_CASE("the templates an installation carries are listed by name")
{
    const std::vector<std::string> names = app::availableTemplates(repositoryRoot() / "templates");
    REQUIRE_FALSE(names.empty());
    CHECK(std::find(names.begin(), names.end(), "starter") != names.end());
    CHECK(std::is_sorted(names.begin(), names.end()));

    // A build tree with nothing staged is an empty list rather than a crash,
    // and the launcher says so rather than offering a button that cannot work.
    CHECK(app::availableTemplates("no-such-directory").empty());
    CHECK(app::availableTemplates({}).empty());
}

TEST_CASE("a created project is a project, with the name substituted and the definitions in it")
{
    seedCatalog();
    Scratch scratch("create");
    const std::filesystem::path templates = repositoryRoot() / "templates";
    const std::filesystem::path definitions = repositoryRoot() / "runtime" / "types" / "engine.d.luau";

    const app::NewProjectResult result = app::createProject(
        templates, definitions, {.parent = scratch.root, .name = "mygame", .templateName = "starter"});
    const std::string why = result.error.has_value() ? result.error->detail : std::string{};
    REQUIRE_MESSAGE(!result.error.has_value(), why);

    CHECK(app::isProjectDirectory(result.path));
    CHECK(platform::fileExists(result.path / "src" / "scripts" / "main.luau"));
    CHECK(platform::fileExists(result.path / ".luaug" / "types" / "engine.d.luau"));

    std::string config;
    REQUIRE(platform::readTextFile(result.path / "luaug.toml", config));
    // The placeholder is gone and the name is in: a scaffolded project that
    // still says `{{name}}` is one nothing substituted.
    CHECK(config.find("{{name}}") == std::string::npos);
    CHECK(config.find("mygame") != std::string::npos);
    CHECK(app::projectNameOf(result.path) == "mygame");
}

TEST_CASE("the launcher and the template it copies agree, file for file")
{
    // **The oracle for having two scaffolders** (ADR 0055). `luaug new` is Lute
    // and cannot be called from here, so what this compares is the launcher's
    // output against the TEMPLATE it came from: every file present, and every
    // one of them identical once the placeholder is substituted. A file the
    // template gains and the copy drops fails here, which is the drift that
    // having two implementations actually risks.
    seedCatalog();
    Scratch scratch("oracle");
    const std::filesystem::path templates = repositoryRoot() / "templates";

    const app::NewProjectResult result =
        app::createProject(templates, {}, {.parent = scratch.root, .name = "oracle", .templateName = "starter"});
    REQUIRE_FALSE(result.error.has_value());

    const std::vector<std::pair<std::string, std::string>> source = treeOf(templates / "starter");
    const std::vector<std::pair<std::string, std::string>> copied = treeOf(result.path);

    REQUIRE_FALSE(source.empty());
    REQUIRE(source.size() == copied.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        CHECK(source[i].first == copied[i].first);
        std::string expected = source[i].second;
        for (std::string::size_type at = expected.find("{{name}}"); at != std::string::npos;
             at = expected.find("{{name}}", at + 6)) {
            expected.replace(at, 8, "oracle");
        }
        CHECK(expected == copied[i].second);
    }
}
