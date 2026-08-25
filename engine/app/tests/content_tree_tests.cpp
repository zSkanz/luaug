// The content browser's model, minus the pixels.
//
// The cases that matter are the ones a person only finds by having a real
// project: a `.scene.json` is not a plain `.json`, a folder called `..` is not a
// folder, and two people's screens have to agree about the order.
#include "luaug/app/content_tree.h"
#include "luaug/asset/material.h"
#include "luaug/platform/file.h"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>

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
    // `stone.material.json` called `stone.material 2.json` is a file the browser
    // no longer recognises as a material -- which is the whole reason the split
    // asks the KIND what the suffix is.
    Scratch scratch("duplicate-compound");
    scratch.folder("content");
    scratch.file("content/stone.material.json", "{\"format\":\"luaug-material\"}");
    scratch.file("content/main.scene.json", "{}");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const app::ContentEntry* material = nullptr;
    for (const app::ContentEntry& entry : tree.entries()) {
        if (entry.name == "stone.material.json")
            material = &entry;
    }
    REQUIRE(material != nullptr);

    const std::string made = tree.duplicate(*material);
    CHECK(made == "stone 2.material.json");
    CHECK(std::filesystem::exists(scratch.root() / "content" / "stone 2.material.json"));
    // And it is still a material as far as the browser is concerned, which is
    // the half a wrong split silently loses.
    CHECK(app::contentKindOf(made) == ContentKind::Material);
    // The original is untouched: everything already pointed at it keeps working,
    // which is why a material is a file rather than a property.
    CHECK(std::filesystem::exists(scratch.root() / "content" / "stone.material.json"));
}

TEST_CASE("duplicating twice numbers upwards, and fills a gap")
{
    Scratch scratch("duplicate-numbers");
    scratch.folder("content");
    scratch.file("content/stone.material.json", "a");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    const auto findByName = [&tree](std::string_view name) -> const app::ContentEntry* {
        for (const app::ContentEntry& entry : tree.entries()) {
            if (entry.name == name)
                return &entry;
        }
        return nullptr;
    };

    REQUIRE(findByName("stone.material.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.material.json")) == "stone 2.material.json");
    REQUIRE(findByName("stone.material.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.material.json")) == "stone 3.material.json");

    // Delete the middle one and the next duplicate takes its place. "First
    // free" rather than a counter that only climbs, which is what somebody
    // expects after tidying up.
    std::error_code ec;
    std::filesystem::remove(scratch.root() / "content" / "stone 2.material.json", ec);
    REQUIRE(tree.refresh());
    REQUIRE(findByName("stone.material.json") != nullptr);
    CHECK(tree.duplicate(*findByName("stone.material.json")) == "stone 2.material.json");
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

TEST_CASE("a material can be made from nothing, which is how one starts")
{
    // **The dead end this closes**: import textures, and then have no way to
    // build a surface out of them. A mesh and a texture arrive from outside and
    // a stamp is made from what is in the world; a material is the one authored
    // file somebody writes.
    Scratch scratch("new-material");
    scratch.folder("content/materials");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.enter("materials"));

    const std::string made = tree.createMaterial("stone");
    CHECK(made == "stone.material.json");
    CHECK(std::filesystem::exists(scratch.root() / "content" / "materials" / "stone.material.json"));
    // The browser knows what it is, which is what puts it in a `Material`
    // property's picker.
    CHECK(app::contentKindOf(made) == ContentKind::Material);

    // And it is a real material: readable, named after the file, and the
    // IDENTITY -- white with no maps, so a part pointed at it looks exactly as
    // it did with none.
    std::string text;
    REQUIRE(platform::readTextFile(scratch.root() / "content" / "materials" / made, text));
    asset::MaterialAsset back;
    REQUIRE_FALSE(asset::readMaterial(text, made, back).has_value());
    CHECK(back.name == "stone");
    CHECK(back.baseColorFactor.r == 1.0f);
    CHECK_FALSE(back.baseColor.present());
}

TEST_CASE("the material suffix is put back rather than required")
{
    // Typing `stone` means a material called stone, and typing the whole file
    // name means the same thing. The same rule `rename` follows, for the same
    // reason: the suffix is what makes it a material and typing a name is not
    // asking to stop being one.
    Scratch scratch("new-material-suffix");
    scratch.folder("content");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    CHECK(tree.createMaterial("stone") == "stone.material.json");
    CHECK(tree.createMaterial("brick.material.json") == "brick.material.json");
    // Case is not a second name: a filesystem that ignores it would otherwise
    // let two files claim one.
    CHECK(tree.createMaterial("slate.MATERIAL.JSON") == "slate.MATERIAL.JSON");
}

TEST_CASE("a material is refused rather than overwriting one already there")
{
    // Two files with one name is a question, and answering it by destroying one
    // of them is not an answer -- especially this one, which somebody may have
    // spent an afternoon on.
    Scratch scratch("new-material-clash");
    scratch.folder("content");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.createMaterial("stone") == "stone.material.json");

    // Written over, this would come back as the default block and take the
    // person's work with it.
    scratch.file("content/stone.material.json",
                 R"({"format":"luaug-material","name":"mine","baseColorFactor":[0.5,0.25,0.125]})");

    CHECK(tree.createMaterial("stone").empty());
    CHECK(tree.createMaterial("stone.material.json").empty());

    std::string text;
    REQUIRE(platform::readTextFile(scratch.root() / "content" / "stone.material.json", text));
    CHECK(text.find("mine") != std::string::npos);
}

TEST_CASE("a name a filesystem cannot carry is refused")
{
    Scratch scratch("new-material-names");
    scratch.folder("content");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));

    // The same rule every other name in this browser follows.
    CHECK(tree.createMaterial("").empty());
    CHECK(tree.createMaterial("   ").empty());
    CHECK(tree.createMaterial(".").empty());
    CHECK(tree.createMaterial("..").empty());
    CHECK(tree.createMaterial("a/b").empty());
    CHECK(tree.createMaterial("../escape").empty());
}

TEST_CASE("a material lands in the folder the browser is looking at")
{
    // Which is what "new material" means to somebody standing in a folder --
    // and the reason the toolbar button and the folder's own menu reach the
    // same call.
    Scratch scratch("new-material-folder");
    scratch.folder("content/props/textures");

    app::ContentTree tree;
    REQUIRE(tree.open(scratch.root() / "content"));
    REQUIRE(tree.enter("props"));
    REQUIRE(tree.enter("textures"));

    REQUIRE(tree.createMaterial("bark") == "bark.material.json");
    CHECK(std::filesystem::exists(scratch.root() / "content" / "props" / "textures" / "bark.material.json"));
    CHECK_FALSE(std::filesystem::exists(scratch.root() / "content" / "bark.material.json"));
    // And the tree re-read, so it is on screen without anybody refreshing.
    bool listed = false;
    for (const app::ContentEntry& entry : tree.entries())
        listed = listed || entry.name == "bark.material.json";
    CHECK(listed);
}

TEST_CASE("a tree with no root makes nothing")
{
    app::ContentTree tree;
    CHECK(tree.createMaterial("stone").empty());
}
