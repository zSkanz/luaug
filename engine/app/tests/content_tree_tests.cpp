// The content browser's model, minus the pixels.
//
// The cases that matter are the ones a person only finds by having a real
// project: a `.scene.json` is not a plain `.json`, a folder called `..` is not a
// folder, and two people's screens have to agree about the order.
#include "luaug/app/content_tree.h"

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
