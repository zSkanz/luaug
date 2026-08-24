// The project browser's model (ADR 0055).
//
// **This is the half of the launcher that can be asserted on**: which projects
// are remembered and in what order, whether a directory is one at all, which
// templates an installation carries, and what making a project actually writes.
// The ImGui half lives in `debug_overlay.cpp` and draws exactly what this
// decides -- the same split `inspector.h` established, for the same reason.
// There is no picture a test can hold, and everything else here is.
//
// R3 does not apply to the text here, for the reason `debug_overlay.h` states:
// the launcher exists for whoever is building a game, never for a player.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app {

// One row of the list.
struct RecentProject
{
    std::filesystem::path path;
    // What the project calls itself: `[project] name` from its `luaug.toml`, or
    // the directory's own name when it does not say.
    std::string name;
    // The directory is gone, or has stopped being a project. **Shown rather than
    // dropped**: a list that edits itself when a drive is unplugged is a list
    // somebody cannot trust, and "where did my project go" is worse than a row
    // that says so and offers to remove itself.
    bool missing = false;
};

// Whether the engine could open this directory: it holds a `luaug.toml`, or the
// `src/scripts` tree that makes a project without one. The same two questions
// `tools/cli/project.luau` asks, because a directory the CLI would run and the
// launcher would refuse is a disagreement about what a project is.
[[nodiscard]] bool isProjectDirectory(const std::filesystem::path& directory);

// What a project calls itself, for a row's label. Falls back to the directory
// name, which is what an unnamed project is called everywhere else.
[[nodiscard]] std::string projectNameOf(const std::filesystem::path& directory);

// The projects this user has opened, most recent first.
//
// **Order is the file's order and there is no clock anywhere in this.** A
// timestamp would be a second thing to keep true for a list whose only ordering
// question is "which did I open last", and the answer to that is "the one at the
// front". It also keeps a wall clock out of a header that `app` includes.
class ProjectList
{
public:
    // Reads `file`, dropping nothing: an entry whose directory is gone comes
    // back marked `missing`. A file that does not exist is an empty list rather
    // than an error -- the first launch is not a failure.
    void load(const std::filesystem::path& file);

    // Writes the list back to wherever `load` read it. False when there is
    // nowhere to write -- `platform::paths().userDir` is empty on a platform
    // with no user directory, and a launcher that cannot remember still runs.
    [[nodiscard]] bool save() const;

    // Moves `path` to the front, adding it when it is new. Deduplicated by the
    // canonical path, so opening the same project through a different spelling
    // does not produce two rows.
    void remember(const std::filesystem::path& path);

    // Removes it. The only way an entry leaves the list, including a missing
    // one -- see `RecentProject::missing`.
    void forget(const std::filesystem::path& path);

    // Re-asks the filesystem about every entry. Cheap, and called when the
    // launcher regains focus rather than every frame.
    void refresh();

    [[nodiscard]] std::span<const RecentProject> entries() const noexcept { return entries_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::vector<RecentProject> entries_;
    std::filesystem::path file_;
};

// The templates an installation carries, in name order. Empty when the
// directory is absent, which is what a build tree with no staged content looks
// like -- and the launcher says so rather than offering a Create button that
// cannot work.
[[nodiscard]] std::vector<std::string> availableTemplates(const std::filesystem::path& templatesDir);

// Whether this is a usable project name. Letters, digits, dashes and
// underscores -- the same rule `luaug new` applies, because a name one accepts
// and the other refuses is a name that depends on which door you came through.
[[nodiscard]] bool validProjectName(std::string_view name);

struct NewProjectRequest
{
    // Where the project directory is created.
    std::filesystem::path parent;
    std::string name;
    std::string templateName;
};

struct NewProjectResult
{
    std::filesystem::path path;
    std::optional<core::EngineError> error;
};

// Copies a template into `parent/name`, substituting `{{name}}`, and writes the
// generated engine definitions into `.luaug/types/`.
//
// **The second implementation of what `luaug new` does, and it is watched rather
// than trusted** (ADR 0055). The CLI keeps its own because a CLI that needed a
// window to make a project would be a worse CLI; what makes two safe is that
// both read the same template directory and a test compares the trees they
// produce, file by file.
//
// Refuses rather than merges when the target exists: a Create button that wrote
// into somebody's existing folder is the one mistake here that costs work.
[[nodiscard]] NewProjectResult createProject(const std::filesystem::path& templatesDir,
                                             const std::filesystem::path& definitions,
                                             const NewProjectRequest& request);

// What the launcher panel is looking at, and what it decided while it drew.
//
// The same shape `EditorCommands` has and for the same reason: a panel that
// acted on its own decisions would be starting a process from inside an ImGui
// callback, and the loop that owns the window is where that belongs.
struct LauncherView
{
    // --- What the panel reads.
    ProjectList* projects = nullptr;
    // Where the templates and the generated definitions live: engine content,
    // staged beside the binary.
    std::filesystem::path templatesDir;
    std::filesystem::path definitions;
    // Where a new project goes unless somebody says otherwise.
    std::filesystem::path defaultParent;
    // Whether the Browse button can do anything. False in a build or on a
    // platform with no native picker, and the panel says so rather than
    // offering a button that does nothing.
    bool canBrowse = false;

    // --- What the panel decided, drained by the loop each frame.
    // A project to open. The loop starts the editor on it and stops.
    std::filesystem::path open;
    // Show the system folder picker.
    bool browse = false;
    bool quit = false;

    // The last thing that went wrong or was done, shown under the list. Plain
    // text: the launcher is a developer tool, which is what exempts it from R3
    // exactly as the overlay is exempt.
    std::string message;
};

} // namespace luaug::app
