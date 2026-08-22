--!strict
--[[
	`luaug build` — the distributable player (roadmap M8, api-design.md §4).

	**The product is a folder.** Not an installer, not a self-extracting
	executable, not a single file: a directory holding the renamed host binary,
	the engine's own content, and the game. It is what every gate on this
	machine can execute end to end, and it is what "send it to somebody" means
	without a distribution story v1 does not have.

	The layout, and the one convention that makes it work:

		MyGame/
		  MyGame.exe        the host, copied, wearing the game's icon
		  content/          the engine's own: catalogs, shaders, fonts, @luaug
		  game/             the project: luaug.toml, src/, its built pack

	`luaug-host` given no script mounts `game/` beside itself. That is the whole
	mechanism — a convention rather than a configuration file, because a
	configuration file is a second thing that can go missing.

	**Windows only, and it says so rather than pretending.** The roadmap's
	release line is Windows binaries plus source instructions; a packaging path
	no tier here can execute is a packaging path that ships broken.
]]

local fs = require("@std/fs")
local path = require("@std/path")
local platform = require("@std/system/platform")
local process = require("@std/process")

local buildAssets = require("./build-assets")
local catalog = require("../catalog")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    path: string,
    -- Where the folder goes. Defaults to `dist/<target>` under the project.
    output: string?,
    target: string,
    -- Clear an output directory this command did not write. Without it, an
    -- existing directory with no build marker in it is refused: `luaug build`
    -- deletes what it is pointed at, and being pointed at the wrong thing must
    -- not be silent.
    force: boolean,
}

-- The only target v1 builds. Named rather than derived from the host platform
-- so that running this on Linux says what is missing instead of producing a
-- folder that cannot run anywhere.
local SupportedTarget = "win64"

-- Written into the output so a rebuild may clear it. Its presence is the whole
-- of the safety check; its contents are for a human who finds it.
local MarkerName = ".luaug-build"
local MarkerText = "Written by `luaug build`. This directory is cleared on every rebuild.\n"

local function copyTree(from: string, to: string)
    fs.createDirectory(to, { makeParents = true })
    for _, entry in fs.listDirectory(from) do
        local source = projectlib.joined(from, entry.name)
        local target = projectlib.joined(to, entry.name)
        if entry.type == "dir" then
            copyTree(source, target)
        elseif entry.type == "file" then
            -- `fs.copy` rather than read-then-write: the trees here carry a
            -- font, a content pack and an executable, and a text round-trip
            -- would corrupt all three.
            fs.copy(source, target)
        end
    end
end

local function removeTree(target: string)
    if not fs.exists(target) then
        return
    end
    -- `fs.remove` is for FILES: on Windows it refuses a directory outright, so
    -- a hand-rolled walk removes the leaves and then fails on the first branch.
    fs.removeDirectory(target, { recursive = true })
end

--- `[project] name`, or the directory's own name. Sanitised, because it becomes
--- a filename: a project called `My Game/2` would otherwise produce a path.
local function productName(project: projectlib.Project): string
    local configured: string? = nil
    local section = project.config.project
    if typeof(section) == "table" and typeof(section.name) == "string" then
        configured = section.name
    end
    local name = configured or path.basename(project.root) or "game"
    name = string.gsub(name, "[^%w_%-%.]", "-")
    return if name == "" then "game" else name
end

--- `[project] icon`, resolved against the project root. Nil when the project
--- does not name one, which is not an error: the artifact then wears the
--- engine's mark, which is what `branding/README.md` calls the fallback.
local function iconPath(project: projectlib.Project): string?
    local section = project.config.project
    if typeof(section) ~= "table" or typeof(section.icon) ~= "string" then
        return nil
    end
    local resolved = projectlib.joined(project.root, section.icon)
    return if fs.exists(resolved) then resolved else nil
end

local function run(options: Options): number
    if options.target ~= SupportedTarget then
        print(tr("cli.build.err.unknown_target", { target = options.target, supported = SupportedTarget }))
        return 2
    end
    if not platform.win32 then
        -- Refused rather than attempted. A folder built on Linux would carry a
        -- Linux binary named `.exe`, which is a shipped artifact that cannot
        -- run and cannot be told apart from one that can.
        print(tr("cli.build.err.wrong_host", { target = options.target }))
        return 2
    end

    local project = projectlib.load(options.path)
    if project.diagnostic ~= nil then
        print(project.diagnostic)
        return 1
    end
    if not projectlib.isProject(project) then
        print(tr("cli.err.not_a_project", { path = project.root }))
        return 1
    end

    local engine = projectlib.findEngine()
    if engine == nil then
        print(tr("cli.err.no_engine"))
        return 1
    end
    local engineContent = projectlib.joined(path.dirname(engine), "content")
    if not fs.exists(engineContent) then
        print(tr("cli.build.err.no_engine_content", { path = engineContent }))
        return 1
    end

    -- The content pack first, because the folder ships the built pack rather
    -- than the source art: building it afterwards would copy yesterday's.
    local content = buildAssets.contentDirectory(project)
    if fs.exists(content) then
        local assetResult = buildAssets.run({ path = project.root, output = nil, verify = false })
        if assetResult ~= 0 then
            return assetResult
        end
    end

    local name = productName(project)
    local output = options.output or projectlib.joined(project.root, "dist", options.target)

    if fs.exists(output) then
        if not (fs.exists(projectlib.joined(output, MarkerName)) or options.force) then
            print(tr("cli.build.err.output_exists", { path = output }))
            return 1
        end
        removeTree(output)
    end
    fs.createDirectory(output, { makeParents = true })
    fs.writeStringToFile(projectlib.joined(output, MarkerName), MarkerText)

    -- The engine: its binary under the game's name, and its content beside it.
    local executable = projectlib.joined(output, projectlib.executableName(name))
    fs.copy(engine, executable)
    copyTree(engineContent, projectlib.joined(output, "content"))

    -- The game. Source rather than compiled bytecode -- ADR 0045 says why, and
    -- what would have to exist for that to change.
    local game = projectlib.joined(output, "game")
    fs.createDirectory(game, { makeParents = true })
    for _, entry in { "luaug.toml", ".luaurc" } do
        local source = projectlib.joined(project.root, entry)
        if fs.exists(source) then
            fs.copy(source, projectlib.joined(game, entry))
        end
    end
    for _, directory in { "src", ".luaug" } do
        local source = projectlib.joined(project.root, directory)
        if fs.exists(source) then
            copyTree(source, projectlib.joined(game, directory))
        end
    end
    -- The loose source trees ship only when there is no pack to replace them.
    -- Shipping both would double the size of every game for nothing, and
    -- shipping neither would be a game with no art.
    --
    -- Two directories rather than one, because a project has two conventions in
    -- play: `content/` is what the engine mounts and what `build-assets`
    -- compiles, and `assets/` is what the starter template scaffolds
    -- (api-design.md §4). A build that copied only the one it liked would
    -- produce a game missing its own message catalog.
    local pack = projectlib.joined(project.root, ".luaug", "content.lpack")
    if not fs.exists(pack) then
        if fs.exists(content) then
            copyTree(content, projectlib.joined(game, path.basename(content) or "content"))
        end
        local assets = projectlib.joined(project.root, "assets")
        if fs.exists(assets) and assets ~= content then
            copyTree(assets, projectlib.joined(game, "assets"))
        end
    end

    -- The identity. An icon nobody can assert is an icon that silently
    -- regresses, so this ends by reading the resource back out of the artifact
    -- rather than by trusting the tool that wrote it.
    local icon = iconPath(project)
    if icon ~= nil then
        local tool = projectlib.findTool("iconpatch")
        if tool == nil then
            print(tr("cli.build.warn.no_iconpatch"))
        else
            local patched = process.run({ tool, "replace", executable, icon })
            if not patched.ok then
                print(patched.stdout .. patched.stderr)
                print(tr("cli.build.err.icon_failed", { path = icon }))
                return 1
            end
            local verified = process.run({ tool, "verify", executable })
            if not verified.ok then
                print(verified.stdout .. verified.stderr)
                print(tr("cli.build.err.icon_failed", { path = icon }))
                return 1
            end
            print(tr("cli.build.icon", { path = icon }))
        end
    end

    print(tr("cli.build.done", { path = output, executable = path.basename(executable) or name }))
    return 0
end

return {
    run = run,
    SupportedTarget = SupportedTarget,
    MarkerName = MarkerName,
}
