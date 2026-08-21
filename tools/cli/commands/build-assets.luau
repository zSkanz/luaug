--!strict
--[[
	`luaug build-assets` — compile a project's content directory into a pack.

	The Luau half of the split M7's brief makes (Decision 1): this side owns the
	project model — where the content is, where the output goes, what the
	console says — and `assetc` owns the codecs, because Lute cannot run a mesh
	simplifier and a texture encoder is a C++ library the repository vendors.

	`--verify` is the roadmap's own gate item ("asset build determinism check in
	CI") made into a flag a person can also type: build twice into different
	files and compare the bytes. It is here rather than in a CI script because a
	determinism check that only CI can run is one nobody runs before pushing.
]]

local fs = require("@std/fs")
local process = require("@std/process")

local catalog = require("../catalog")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    path: string,
    -- Where the pack goes. Defaults to `.luaug/content.lpack` under the
    -- project, which is where a build artefact belongs: beside the project,
    -- ignored by git, and not in `src/`.
    output: string?,
    -- Build twice and require the two to be byte-identical.
    verify: boolean,
}

local DefaultContentDir = "content"
local DefaultOutputDir = ".luaug"

--- `[assets] content = "..."` in luaug.toml, or `content/`. A project that
--- keeps its source art elsewhere says so once rather than passing a flag
--- every time.
local function contentDirectory(project: projectlib.Project): string
    local assets = project.config.assets
    if typeof(assets) == "table" and typeof(assets.content) == "string" then
        return projectlib.joined(project.root, assets.content)
    end
    return projectlib.joined(project.root, DefaultContentDir)
end

local function runCompiler(tool: string, input: string, pack: string, manifest: string): (boolean, string)
    local result = process.run({ tool, "--input", input, "--output", pack, "--manifest", manifest })
    local output = result.stdout
    if result.stderr ~= "" then
        output ..= result.stderr
    end
    return result.ok, output
end

local function run(options: Options): number
    local project = projectlib.load(options.path)
    if project.diagnostic ~= nil then
        print(project.diagnostic)
        return 1
    end
    if not projectlib.isProject(project) then
        print(tr("cli.err.not_a_project", { path = project.root }))
        return 1
    end

    local content = contentDirectory(project)
    if not fs.exists(content) then
        -- Named rather than silently producing an empty pack. A build that ran,
        -- said nothing and produced nothing is the failure this project keeps
        -- designing against.
        print(tr("cli.assets.err.no_content", { path = content }))
        return 1
    end

    local tool = projectlib.findTool("assetc")
    if tool == nil then
        print(tr("cli.assets.err.no_compiler"))
        return 1
    end

    local outputDir = projectlib.joined(project.root, DefaultOutputDir)
    if not fs.exists(outputDir) then
        fs.createDirectory(outputDir, { makeParents = true })
    end

    local pack = options.output or projectlib.joined(outputDir, "content.lpack")
    local manifest = projectlib.joined(outputDir, "content.manifest.json")

    local ok, output = runCompiler(tool, content, pack, manifest)
    if not ok then
        print(output)
        print(tr("cli.assets.err.failed"))
        return 1
    end
    print(output)

    if options.verify then
        -- A second build into a second place, compared byte for byte. Both
        -- outputs, because a manifest that matched while the pack differed
        -- would be a cache that hands back the wrong bytes under the right
        -- name.
        local secondPack = `{pack}.verify`
        local secondManifest = `{manifest}.verify`
        local verifiedOk, verifyOutput = runCompiler(tool, content, secondPack, secondManifest)
        if not verifiedOk then
            print(verifyOutput)
            print(tr("cli.assets.err.failed"))
            return 1
        end

        local same = fs.readFileToString(pack) == fs.readFileToString(secondPack)
            and fs.readFileToString(manifest) == fs.readFileToString(secondManifest)
        fs.remove(secondPack)
        fs.remove(secondManifest)

        if not same then
            print(tr("cli.assets.err.not_deterministic"))
            return 1
        end
        print(tr("cli.assets.deterministic"))
    end

    print(tr("cli.assets.done", { pack = pack, manifest = manifest }))
    return 0
end

return {
    run = run,
    contentDirectory = contentDirectory,
}
