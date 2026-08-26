--!strict
--[[
	The tag being released and the version the repository declares must agree
	(S7.12).

	**A release names itself twice**: once in the git tag somebody pushes and
	once in `project(LuauG VERSION ...)`, which ADR 0031 makes the single
	authoritative declaration -- the generated header reads it, `luaug --version`
	reports it, `luaug build` stamps it into every packaged artifact and
	`tools/repo/package.luau` names the archive after it. Two places, one fact,
	and nothing checked that they said the same thing.

	What that costs is not abstract. `v1.1.0` pushed against a tree still
	declaring `1.0.0` produces an archive called `LuauG-1.0.0-win64`, a binary
	whose `--version` says 1.0.0, and a release page saying 1.1.0 -- and the
	first person to report a bug reports it against the wrong version. D086
	records the near-miss from the other direction, where the DECLARATION was
	behind the newest release; `package.luau` has refused that since. This is the
	same check at the moment it actually matters.

	Run with the tag to check:

		lute tools/repo/versioncheck.luau v1.1.0

	With no argument it takes the tag from `GITHUB_REF_NAME`, which is what a
	tag-triggered workflow already has. With neither it says so and fails,
	because a version check that silently passes when it cannot find the tag is
	the check not running.

	**Only `v*` tags are checked.** `milestone/e9` is not a release and names no
	version; it passes without comment, because a milestone tag is a bookmark and
	the whole point of the pattern is that the two kinds are told apart by shape.
]]

local fs = require("@std/fs")
local process = require("@std/process")

--- The version, from the one declaration ADR 0031 makes authoritative.
local function declaredVersion(): string?
    if not fs.exists("CMakeLists.txt") then
        return nil
    end
    return string.match(fs.readFileToString("CMakeLists.txt"), "project%s*%(%s*LuauG%s+VERSION%s+([%d%.]+)")
end

--- The `X.Y.Z` a release tag names, or `nil` for a tag that is not one.
---
--- Deliberately strict: `v1.1` and `v1.1.0-rc1` are not release tags this
--- repository makes, and accepting them would mean deciding what they compare
--- against.
local function releaseVersion(tag: string): string?
    return string.match(tag, "^v(%d+%.%d+%.%d+)$")
end

local function run(): number
    if not fs.exists("CMakeLists.txt") then
        print("versioncheck: run this from the repository root")
        return 2
    end

    -- **Skipping the script's own path**, which `process.args` carries as its
    -- first element -- the same shape `package.luau` steps over, and the reason
    -- `versioncheck.luau` reported itself as a tag before this line existed.
    local given: string? = nil
    for index, argument in process.args do
        if index == 1 and string.sub(argument, -5) == ".luau" then
            continue
        end
        if given == nil then
            given = argument
        end
    end

    local tag = given or process.env.GITHUB_REF_NAME
    if tag == nil or tag == "" then
        print("versioncheck: no tag given and GITHUB_REF_NAME is not set")
        print("  usage: lute tools/repo/versioncheck.luau v1.2.3")
        return 2
    end

    local wanted = releaseVersion(tag)
    if wanted == nil then
        -- A milestone tag is a bookmark, not a release. Said out loud rather
        -- than silent, so a run of this in CI is never mistaken for a check that
        -- passed on a release.
        print(`versioncheck: {tag} is not a release tag; nothing to check`)
        return 0
    end

    local declared = declaredVersion()
    if declared == nil then
        print("versioncheck: no project(LuauG VERSION ...) declaration in CMakeLists.txt")
        return 2
    end

    if declared ~= wanted then
        print(`versioncheck: the tag says {wanted} and CMakeLists.txt declares {declared}.`)
        print("  Everything a release stamps comes from the declaration (ADR 0031): the archive's name,")
        print("  `luaug --version`, and the version in every packaged artifact. Shipping this tag would")
        print("  publish a release page saying one number and a binary saying another.")
        print(`  Fix: set project(LuauG VERSION {wanted}) on the commit the tag points at, or retag.`)
        return 1
    end

    print(`versioncheck: ok -- {tag} and CMakeLists.txt both say {declared}`)
    return 0
end

return process.exit(run())
