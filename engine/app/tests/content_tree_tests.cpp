// The content browser's model, minus the pixels.
//
// The cases that matter are the ones a person only finds by having a real
// project: a `.scene.json` is not a plain `.json`, a folder called `..` is not a
// folder, and two people's screens have to agree about the order.
#include "luaug/app/content_tree.h"
#include "luaug/platform/file.h"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace luaug;
using luaug::app::ContentKind;
using luaug::app::ContentTree;

namespace {
// A scratch tree this test owns. Named after the case so a failure leaves
// something a person can look at, and removed on the way in rather than the way
// out for the same reason.
class Scratch
{
public:
    explicit Scratch(std::string_view name)
    {
        m_root = std::filesystem::temp_directory_path() / "luaug-content-tests" / name;
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }

    void file(std::string_view relative, std::string_view contents = "x") const
    {
        const std::filesystem::path path = m_root / std::filesystem::path(relative);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }

    void folder(std::string_view relative) const
    {
        std::error_code ec;
        std::filesystem::create_directories(m_root / std::filesystem::path(relative), ec);
    }

private:
    std::filesystem::path m_root;
};
} // namespace

TEST_CASE("a scene is not a plain json file")
{
    // The specific suffix has to be asked about first. Getting this backwards
    // makes every scene in a project a file of unknown kind, and the browser
    // that shows them offers no way to open one.
    CHECK(app::contentKindOf("main.scene.json") == ContentKind::Scene);
    CHECK(app::contentKindOf("MAIN.SCENE.JSON") == ContentKind::Scene);
    CHECK(app::contentKindOf("cell_0_0.chunk.json") == ContentKind::Chunk);
    CHECK(app::contentKindOf("manifest.json") == ContentKind::Other);

    CHECK(app::contentKindOf("tower.glb") == ContentKind::Mesh);
    CHECK(app::contentKindOf("bark.png") == ContentKind::Texture);
    CHECK(app::contentKindOf("notes.txt") == ContentKind::Other);
    CHECK(app::contentKindOf("") == ContentKind::Other);
}

TEST_CASE("folders come first and names are ordered, so two screens agree")
{
    const Scratch scratch("ordering");
    scratch.file("zebra.glb");
    scratch.file("Alpha.png");
    scratch.folder("models");
    scratch.folder("Audio");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));

    REQUIRE(tree.entries().size() == 4);
    CHECK(tree.entries()[0].name == "Audio");
    CHECK(tree.entries()[1].name == "models");
    CHECK(tree.entries()[2].name == "Alpha.png");
    CHECK(tree.entries()[3].name == "zebra.glb");
}

TEST_CASE("walking in and out of folders keeps the relative path honest")
{
    const Scratch scratch("walking");
    scratch.file("scenes/levels/first.scene.json");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));
    CHECK(tree.atRoot());

    REQUIRE(tree.enter("scenes"));
    CHECK(tree.currentFolder() == "scenes");
    CHECK_FALSE(tree.atRoot());

    REQUIRE(tree.enter("levels"));
    CHECK(tree.currentFolder() == "scenes/levels");
    REQUIRE(tree.entries().size() == 1);
    CHECK(tree.entries()[0].kind == ContentKind::Scene);
    CHECK(tree.entries()[0].path == "scenes/levels/first.scene.json");

    REQUIRE(tree.leave());
    CHECK(tree.currentFolder() == "scenes");
    REQUIRE(tree.leave());
    CHECK(tree.atRoot());

    // Nowhere further up, and asking is not an error: a caller should not have
    // to check before pressing a button.
    CHECK_FALSE(tree.leave());
    CHECK(tree.atRoot());
}

TEST_CASE("a name cannot climb out of the folder it was typed in")
{
    CHECK_FALSE(ContentTree::isUsableName(""));
    CHECK_FALSE(ContentTree::isUsableName("."));
    CHECK_FALSE(ContentTree::isUsableName(".."));
    CHECK_FALSE(ContentTree::isUsableName("../secrets"));
    CHECK_FALSE(ContentTree::isUsableName("a/b"));
    CHECK_FALSE(ContentTree::isUsableName("a\\b"));
    CHECK_FALSE(ContentTree::isUsableName("C:"));
    CHECK(ContentTree::isUsableName("levels"));
    CHECK(ContentTree::isUsableName("my scenes"));
}

TEST_CASE("entering something that is not a folder does nothing")
{
    const Scratch scratch("not-a-folder");
    scratch.file("tower.glb");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));

    CHECK_FALSE(tree.enter("tower.glb"));
    CHECK(tree.atRoot());

    CHECK_FALSE(tree.enter("nothing-here"));
    CHECK(tree.atRoot());

    // And the traversal a name check would have caught anyway.
    CHECK_FALSE(tree.enter(".."));
    CHECK(tree.atRoot());
}

TEST_CASE("creating a folder makes it and shows it")
{
    const Scratch scratch("create");
    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));
    CHECK(tree.entries().empty());

    REQUIRE(tree.createFolder("scenes"));
    REQUIRE(tree.entries().size() == 1);
    CHECK(tree.entries()[0].kind == ContentKind::Folder);
    CHECK(tree.entries()[0].name == "scenes");

    // Already there. Refused rather than silently succeeding, because "make me a
    // folder" and "there is already one" are different answers.
    CHECK_FALSE(tree.createFolder("scenes"));
    CHECK_FALSE(tree.createFolder("../escape"));
    CHECK(tree.entries().size() == 1);
}

TEST_CASE("a project with no content directory is a normal state and not an error")
{
    ContentTree tree;
    CHECK_FALSE(tree.open("this-path-does-not-exist"));
    CHECK(tree.entries().empty());
    // Every example before `06-scene` is exactly this: nothing authored, so
    // nothing to browse. It must not be a message.
    CHECK(tree.atRoot());
}

TEST_CASE("a dotfile is not content")
{
    const Scratch scratch("dotfiles");
    scratch.file(".gitignore");
    scratch.file("tower.glb");
    scratch.folder(".luaug");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));

    REQUIRE(tree.entries().size() == 1);
    CHECK(tree.entries()[0].name == "tower.glb");
}

TEST_CASE("importing copies a file from anywhere into the folder that is open")
{
    // **This is the whole of what importing an asset is here.** `content/` holds
    // files, `ContentMounts` resolves a loose one, and a mesh that is in there
    // is one a project can name -- so there is no conversion step to test, only
    // the copy and what it refuses.
    Scratch scratch("import");
    scratch.folder("content");
    scratch.folder("elsewhere");
    scratch.file("elsewhere/tree.gltf", "{}");
    scratch.file("elsewhere/bark.png", "not really a png");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.createFolder("props"));
    REQUIRE(tree.enter("props"));

    const std::array<std::filesystem::path, 2> sources{scratch.root() / "elsewhere" / "tree.gltf",
                                                       scratch.root() / "elsewhere" / "bark.png"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    CHECK(report.imported.size() == 2);
    CHECK(report.skipped.empty());
    CHECK(report.failed.empty());
    // In the folder that was open, not at the root.
    CHECK(std::filesystem::exists(scratch.root() / "content" / "props" / "tree.gltf"));
    CHECK_FALSE(std::filesystem::exists(scratch.root() / "content" / "tree.gltf"));
    // And the browser is showing them without anybody asking it to re-read.
    CHECK(tree.entries().size() == 2);
}

TEST_CASE("importing over something that is already there is refused, not silent")
{
    // The one mistake here that costs work: replacing a file somebody has
    // already put work into. Named in the report so the browser can say which.
    Scratch scratch("import-clash");
    scratch.folder("content");
    scratch.file("elsewhere/shared.png", "the new one");
    scratch.file("content/shared.png", "the one already here");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const std::array<std::filesystem::path, 1> sources{scratch.root() / "elsewhere" / "shared.png"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    CHECK(report.imported.empty());
    REQUIRE(report.skipped.size() == 1);
    CHECK(report.skipped[0] == "shared.png");

    std::ifstream kept(scratch.root() / "content" / "shared.png", std::ios::binary);
    std::string text;
    std::getline(kept, text);
    CHECK(text == "the one already here");
}

TEST_CASE("importing a folder is skipped rather than copied recursively")
{
    // A different request, and doing it by accident because somebody
    // multi-selected one is not an outcome to design for.
    Scratch scratch("import-folder");
    scratch.folder("content");
    scratch.folder("elsewhere/a-whole-folder");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const std::array<std::filesystem::path, 1> sources{scratch.root() / "elsewhere" / "a-whole-folder"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    CHECK(report.imported.empty());
    CHECK(report.skipped.size() == 1);
    CHECK_FALSE(std::filesystem::exists(scratch.root() / "content" / "a-whole-folder"));
}

// --- A `.gltf` is a file that names other files -----------------------------
//
// Dragging one in without them imports something that parses and loads nothing:
// the geometry is in a `.bin` beside it and the textures are in a folder beside
// that. A person reported it as "não sei se é eu que estou importando errado mas
// a malha não aparece", which is exactly what it looks like from the outside.

TEST_CASE("importing a glTF brings the files it names")
{
    Scratch scratch("import-gltf");
    scratch.folder("content");
    scratch.folder("downloaded");
    scratch.folder("downloaded/textures");
    // The shape a real export has: an external buffer, images in a subfolder,
    // one image inlined as a data URI, and a per cent-encoded name.
    scratch.file("downloaded/scene.gltf", R"({
        "buffers": [{"uri": "scene.bin"}],
        "images": [
            {"uri": "textures/body%20diffuse.png"},
            {"uri": "data:image/png;base64,AAAA"},
            {"uri": "textures/eye.png"}
        ]
    })");
    scratch.file("downloaded/scene.bin", "binary");
    scratch.file("downloaded/textures/body diffuse.png", "png");
    scratch.file("downloaded/textures/eye.png", "png");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.createFolder("models"));
    REQUIRE(tree.enter("models"));

    const std::array<std::filesystem::path, 1> sources{scratch.root() / "downloaded" / "scene.gltf"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    // One file chosen, three brought: the data URI names nothing and is not one
    // of them.
    CHECK(report.imported.size() == 1);
    CHECK(report.companions.size() == 3);
    CHECK(report.missing.empty());
    CHECK(report.failed.empty());

    const std::filesystem::path models = scratch.root() / "content" / "models";
    CHECK(std::filesystem::exists(models / "scene.gltf"));
    CHECK(std::filesystem::exists(models / "scene.bin"));
    // **Into the same relative place**, because the URIs inside are relative and
    // rewriting them would be editing somebody's asset. Per cent-decoded,
    // because a URI is not a path.
    CHECK(std::filesystem::exists(models / "textures" / "body diffuse.png"));
    CHECK(std::filesystem::exists(models / "textures" / "eye.png"));
}

TEST_CASE("a glTF whose buffer is not beside it says so rather than importing quietly")
{
    // The report's own case: the file copies perfectly, the model loads nothing,
    // and this is the only moment anybody can be told which file to go and find.
    Scratch scratch("import-gltf-missing");
    scratch.folder("content");
    scratch.folder("downloaded");
    scratch.file("downloaded/scene.gltf", R"({"buffers": [{"uri": "scene.bin"}]})");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const std::array<std::filesystem::path, 1> sources{scratch.root() / "downloaded" / "scene.gltf"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    CHECK(report.imported.size() == 1);
    CHECK(report.companions.empty());
    REQUIRE(report.missing.size() == 1);
    CHECK(report.missing[0] == "scene.bin");
    // Nothing FAILED: nothing failed to copy, something was never there to copy,
    // and telling the two apart is what makes the sentence useful.
    CHECK(report.failed.empty());
}

TEST_CASE("a file that names nothing brings nothing")
{
    // A `.glb` has its buffers inside it and a `.png` names nothing at all.
    // Neither should acquire a companion, and nothing here has to know why --
    // one has no URIs and the other is not read.
    Scratch scratch("import-plain");
    scratch.folder("content");
    scratch.folder("downloaded");
    scratch.file("downloaded/model.glb", "glTF binary, not really");
    scratch.file("downloaded/bark.png", "png");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const std::array<std::filesystem::path, 2> sources{scratch.root() / "downloaded" / "model.glb",
                                                       scratch.root() / "downloaded" / "bark.png"};
    const app::ContentTree::ImportReport report = tree.import(sources);

    CHECK(report.imported.size() == 2);
    CHECK(report.companions.empty());
    CHECK(report.missing.empty());
}

TEST_CASE("duplicating keeps a compound extension whole")
{
    // **The case a naive split at the last dot gets wrong.** A duplicate of
    // `main.scene.json` called `main.scene 2.json` is a file the browser no
    // longer recognises as a scene -- which is the whole reason the split asks
    // the KIND what the suffix is.
    Scratch scratch("duplicate-compound");
    scratch.folder("content");
    scratch.file("content/main.scene.json", "{}");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const app::ContentEntry* scene = nullptr;
    for (const app::ContentEntry& entry : tree.entries()) {
        if (entry.name == "main.scene.json")
            scene = &entry;
    }
    REQUIRE(scene != nullptr);

    const std::string made = tree.duplicate(*scene);
    CHECK(made == "main 2.scene.json");
    CHECK(std::filesystem::exists(scratch.root() / "content" / "main 2.scene.json"));
    // And it is still a scene as far as the browser is concerned, which is the
    // half a wrong split silently loses.
    CHECK(app::contentKindOf(made) == ContentKind::Scene);
    // The original is untouched.
    CHECK(std::filesystem::exists(scratch.root() / "content" / "main.scene.json"));
}

TEST_CASE("duplicating twice numbers upwards, and fills a gap")
{
    Scratch scratch("duplicate-numbers");
    scratch.folder("content");
    scratch.file("content/stone.scene.json", "a");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const auto findByName = [&tree](std::string_view name) -> const app::ContentEntry* {
        for (const app::ContentEntry& entry : tree.entries()) {
            if (entry.name == name)
                return &entry;
        }
        return nullptr;
    };

    REQUIRE(findByName("stone.scene.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.scene.json")) == "stone 2.scene.json");
    REQUIRE(findByName("stone.scene.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.scene.json")) == "stone 3.scene.json");

    // Delete the middle one and the next duplicate takes its place. "First
    // free" rather than a counter that only climbs, which is what somebody
    // expects after tidying up.
    std::error_code ec;
    std::filesystem::remove(scratch.root() / "content" / "stone 2.scene.json", ec);
    REQUIRE(tree.refresh());
    REQUIRE(findByName("stone.scene.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.scene.json")) == "stone 2.scene.json");
}

TEST_CASE("duplicating a plain file keeps its own extension")
{
    Scratch scratch("duplicate-plain");
    scratch.folder("content");
    scratch.file("content/rock.gltf", "{}");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const app::ContentEntry* mesh = nullptr;
    for (const app::ContentEntry& entry : tree.entries()) {
        if (entry.name == "rock.gltf")
            mesh = &entry;
    }
    REQUIRE(mesh != nullptr);
    CHECK(tree.duplicate(*mesh) == "rock 2.gltf");
}

TEST_CASE("duplicating a folder brings everything under it")
{
    Scratch scratch("duplicate-folder");
    scratch.folder("content");
    scratch.file("content/props/crate.gltf", "{}");
    scratch.file("content/props/textures/crate.png", "p");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const app::ContentEntry* folder = nullptr;
    for (const app::ContentEntry& entry : tree.entries()) {
        if (entry.kind == ContentKind::Folder && entry.name == "props")
            folder = &entry;
    }
    REQUIRE(folder != nullptr);

    CHECK(tree.duplicate(*folder) == "props 2");
    // A folder means what is in it. Copying the folder and not its contents is
    // an empty folder wearing a familiar name.
    CHECK(std::filesystem::exists(scratch.root() / "content" / "props 2" / "crate.gltf"));
    CHECK(std::filesystem::exists(scratch.root() / "content" / "props 2" / "textures" / "crate.png"));
}

TEST_CASE("walking back up a breadcrumb does not read past the path it shortened")
{
    // **Reported as: three folders deep, click the path to go back, the window
    // closes with nothing in the console.** That is what an uncaught
    // `std::out_of_range` looks like from outside.
    //
    // The breadcrumb held `currentFolder()` -- a REFERENCE into the tree -- and
    // `leave()` assigns to that very string. Clicking a crumb navigated in the
    // middle of the loop, so the next step read `substr(begin, ...)` with a
    // `begin` measured against the old, longer path. Three deep is the shallowest
    // depth that reaches a second step after a click.
    //
    // ImGui cannot be driven headlessly, so this is the loop's ARITHMETIC over
    // the real tree: the same reads, in the same order, with the same navigation
    // in the middle.
    Scratch scratch("breadcrumb-depth");
    scratch.folder("content/a/b/c");
    scratch.file("content/a/b/c/one.png", "p");
    scratch.file("content/a/b/c/two.png", "p");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.enter("a"));
    REQUIRE(tree.enter("b"));
    REQUIRE(tree.enter("c"));
    REQUIRE(tree.currentFolder() == "a/b/c");

    // The loop, as the panel runs it.
    const std::string relative = tree.currentFolder();
    int depth = relative.empty() ? 0 : 1;
    for (const char c : relative)
        depth += c == '/' ? 1 : 0;
    REQUIRE(depth == 3);

    int climb = 0;
    std::size_t begin = 0;
    for (int step = 0; step < depth; ++step) {
        const std::size_t slash = relative.find('/', begin);
        // The read that used to throw once the tree had been shortened.
        const std::string segment =
            relative.substr(begin, slash == std::string::npos ? std::string::npos : slash - begin);
        const std::string here = slash == std::string::npos ? relative : relative.substr(0, slash);
        begin = slash == std::string::npos ? relative.size() : slash + 1;

        if (step == 0) {
            CHECK(segment == "a");
            CHECK(here == "a");
            // The click: recorded, not acted on.
            climb = depth - step - 1;
        }
        if (step == 1)
            CHECK(segment == "b");
        if (step == 2)
            CHECK(segment == "c");
    }

    for (int up = 0; up < climb; ++up)
        REQUIRE(tree.leave());

    CHECK(tree.currentFolder() == "a");
    // And the folder it arrived at is real: `b` is in it, and the PNGs are not.
    bool sawB = false;
    for (const app::ContentEntry& entry : tree.entries())
        sawB = sawB || entry.name == "b";
    CHECK(sawB);
}

TEST_CASE("the current folder survives the navigation that changes it")
{
    // The root cause, stated as its own case: `currentFolder()` used to hand out
    // a reference to the string `enter` and `leave` assign to. Anything that
    // held it across a navigation was reading a string that had changed length
    // underneath it.
    Scratch scratch("breadcrumb-lifetime");
    scratch.folder("content/a/b/c");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.enter("a"));
    REQUIRE(tree.enter("b"));

    const std::string held = tree.currentFolder();
    REQUIRE(tree.leave());
    REQUIRE(tree.leave());

    // Still the path it was taken at, whatever the tree did afterwards.
    CHECK(held == "a/b");
    CHECK(tree.currentFolder().empty());
}

// --- Every stamp in the project ----------------------------------------------

namespace {

// A stamp file, minus everything a reader does not need to answer "what class is
// this a file of".
[[nodiscard]] std::string stampOf(std::string_view className)
{
    return std::string(R"({"format":"luaug-scene","version":1,"root":{"class":")") + std::string(className) +
           R"(","name":"Thing","properties":{}}})";
}

} // namespace

TEST_CASE("the project's stamps are found wherever they are filed")
{
    // **The browser is a place and this is a question.** A reference picker
    // asking "which materials could this part use" must not answer differently
    // depending on which folder somebody last clicked into -- so this walks the
    // whole content root rather than reading the panel's current listing.
    Scratch scratch("collect-stamps");
    scratch.file("stamps/wooden.stamp.json", stampOf("Material"));
    scratch.file("stamps/props/lantern.stamp.json", stampOf("Model"));
    scratch.file("materials/deep/nested/metal.stamp.json", stampOf("Material"));
    // Not stamps, and each is a way the walk could go wrong: a scene shares the
    // extension's tail, a texture shares the folder, and a plain JSON is what a
    // sloppy suffix check would take.
    scratch.file("scenes/main.scene.json", "{}");
    scratch.file("stamps/notes.json", "{}");
    scratch.file("stamps/wood.png", "x");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));

    const std::vector<app::ContentEntry> found = tree.collectStamps();
    REQUIRE(found.size() == 3);

    // Sorted by path, so two machines listing one project agree -- the same rule
    // the folder listing follows and for the same reason.
    CHECK(found[0].path == "materials/deep/nested/metal.stamp.json");
    CHECK(found[1].path == "stamps/props/lantern.stamp.json");
    CHECK(found[2].path == "stamps/wooden.stamp.json");

    // The class each is a file OF, which is what a picker filters on. A stamp
    // whose class could not be read comes back empty rather than absent, and an
    // empty one is refused by every filter.
    CHECK(found[0].rootClass == "Material");
    CHECK(found[1].rootClass == "Model");
    CHECK(found[2].rootClass == "Material");

    // Forward slashes on every platform: the path goes into a scene file, and a
    // backslash there means the file only opens on the machine that wrote it.
    for (const app::ContentEntry& entry : found)
        CHECK(entry.path.find('\\') == std::string::npos);
}

TEST_CASE("the build output is not part of the project")
{
    // `.luaug` holds the import cache and the packed content, and walking it
    // would be walking the build output -- which on a real project is thousands
    // of files and, worse, would offer a compiled copy of a stamp beside the
    // stamp itself.
    Scratch scratch("collect-stamps-dot");
    scratch.file("stamps/real.stamp.json", stampOf("Material"));
    scratch.file(".luaug/import/cached.stamp.json", stampOf("Material"));
    scratch.file(".hidden/also.stamp.json", stampOf("Material"));

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));

    const std::vector<app::ContentEntry> found = tree.collectStamps();
    REQUIRE(found.size() == 1);
    CHECK(found[0].path == "stamps/real.stamp.json");
}

TEST_CASE("a project with no content answers with nothing rather than failing")
{
    // Which is the ordinary state of a project somebody has just made, and a
    // picker that threw here would be a picker that cannot be opened.
    Scratch scratch("collect-stamps-empty");

    ContentTree tree;
    REQUIRE(tree.open(scratch.root()));
    CHECK(tree.collectStamps().empty());

    // And a tree that was never opened at all, which is every shell with no
    // project -- the F3 overlay over a running game has one of those.
    ContentTree unopened;
    CHECK(unopened.collectStamps().empty());
}
