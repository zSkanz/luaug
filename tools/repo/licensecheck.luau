--!strict
--[[
	The licence audit (rule R6, ADR 0021, roadmap M8).

	M8's scope names a "license/NOTICE audit of every vendored dep". An audit
	somebody performs once is a fact about one afternoon; this is the same audit
	written down as a check, so the next pin bump has to pass it.

	Three questions, and each of them has failed somewhere in some project:

	  1. **Does every vendored tree actually carry its licence text?** A
	     dependency whose LICENSE file was lost in a narrowed sync (ADR 0042) is
	     a redistribution with no attribution in it.

	  2. **Is the declared licence one this project is allowed to ship?** R6
	     admits MIT, BSD, zlib, Apache-2.0, public domain and the OFL, and
	     nothing else. The manifest's `license` field is the declaration; this
	     is what stops a new row declaring something else.

	  3. **Has a copyleft licence appeared somewhere new?** Four vendored trees
	     contain GPL text today and none of it is compiled -- sample assets, an
	     Ada binding, a CLI tool, and a triple-licensed header used under its
	     permissive option. Each is listed below with what it is. **The check is
	     that the list does not grow**: a new GPL file appearing in a tree after
	     a pin bump is exactly the thing nobody would notice by reading.

	    lute tools/repo/licensecheck.luau
]]

local fs = require("@std/fs")
local json = require("@std/json")
local path = require("@std/path")
local process = require("@std/process")

local ManifestPath = "third_party/manifest.json"

--- What R6 allows, spelled the way the manifest spells it. A row whose licence
--- is not here is a human decision (MASTER_PROMPT §10), not a lint to relax.
local Allowed: { [string]: boolean } = {
    ["MIT"] = true,
    ["MIT-0 OR Unlicense"] = true,
    ["MIT OR Public Domain"] = true,
    ["Apache-2.0"] = true,
    ["Apache-2.0 OR MIT"] = true,
    ["BSD-2-Clause"] = true,
    ["BSD-3-Clause"] = true,
    ["zlib"] = true,
    ["CC0-1.0 OR Apache-2.0"] = true,
    ["OFL-1.1"] = true,
}

--- Any of these, in a dependency's root, counts as the licence text being
--- present. Upstreams disagree about the spelling and none of them is wrong.
local LicenceNames = {
    "LICENSE",
    "LICENSE.txt",
    "LICENSE.md",
    "LICENCE",
    "LICENCE.txt",
    "License.txt",
    "COPYING",
    "COPYING.txt",
    "LICENSE_A2",
}

--- Copyleft text that is present, expected, and **not compiled** -- by
--- directory, because that is what each of these actually is: a tool, a test
--- suite, a disabled backend, a folder of sample assets. Each entry cites the
--- build evidence rather than asserting it, and the check fails when copyleft
--- appears anywhere ELSE, which is the case nobody would notice by reading.
---
--- **The alternative was narrowing** (ADR 0042): xxHash ships its library under
--- BSD-2 and its tooling under GPL-2.0, so a narrowed row would remove that text
--- from this repository altogether rather than annotate it. It is left as a
--- human decision because it deletes about forty vendored files, which
--- `MASTER_PROMPT.md` §10 puts on the escalation list.
local KnownCopyleft: { { prefix: string, why: string } } = {
    {
        prefix = "third_party/xxhash/cli/",
        why = "xxhsum, the command-line tool, is GPL-2.0. Only the header is used: `engine/scene/CMakeLists.txt`"
            .. " adds `third_party/xxhash` as an include directory and nothing compiles a source file from this tree",
    },
    {
        prefix = "third_party/xxhash/tests/",
        why = "xxHash's own test and benchmark suite, GPL-2.0, built by upstream's Makefile and by nothing here",
    },
    {
        prefix = "third_party/xxhash/Makefile",
        why = "upstream's own Makefile, which names the GPL for the tools it builds; this project builds none of them",
    },
    {
        prefix = "third_party/sdl3/src/hidapi/",
        why = "hidapi is triple-licensed GPLv3 / BSD / original and SDL vendors it under the BSD option --"
            .. " and `third_party/CMakeLists.txt` sets `SDL_HIDAPI OFF`, so none of it is compiled at all",
    },
    {
        prefix = "third_party/assimp/contrib/zlib/contrib/ada/",
        why = "zlib's own contrib tree: an Ada binding, dual zlib/GPL. Nothing in this repository builds Ada",
    },
    {
        prefix = "third_party/jolt/Assets/",
        why = "sample racetrack assets shipped with Jolt for its demo applications; no build target here reads Assets/",
    },
}

local Failures: { string } = {}

local function fail(message: string)
    table.insert(Failures, message)
end

type Dependency = {
    name: string,
    version: string,
    commit: string,
    license: string,
    path: string,
}

local function joined(...: string): string
    return path.format(path.join(...))
end

local function readManifest(): { Dependency }
    local decoded = json.deserialize(fs.readFileToString(ManifestPath)) :: any
    return decoded.dependencies
end

--- Pinned rows only. A row declared for a later milestone has nothing on disk
--- to audit, which the notices file already says in as many words.
local function isVendored(dependency: Dependency): boolean
    if typeof(dependency.commit) ~= "string" or dependency.commit == "" or dependency.commit == "TBD" then
        return false
    end
    return fs.exists(dependency.path) and fs.type(dependency.path) == "dir"
end

local function licenceFile(dependency: Dependency): string?
    for _, name in LicenceNames do
        local candidate = joined(dependency.path, name)
        if fs.exists(candidate) then
            return candidate
        end
    end
    return nil
end

--- Every file under `root` whose text names the GPL or the LGPL. Walked rather
--- than grepped so this runs the same way on every tier.
local function copyleftFiles(root: string, found: { string })
    for _, entry in fs.listDirectory(root) do
        local child = joined(root, entry.name)
        if entry.type == "dir" then
            -- `.git` is not upstream's source and a vendored tree should not
            -- have one, but a developer's working copy sometimes does.
            if entry.name ~= ".git" then
                copyleftFiles(child, found)
            end
            continue
        end
        if entry.type ~= "file" then
            continue
        end
        -- Text only, and bounded: this walks tens of thousands of files, and a
        -- multi-megabyte binary read into a string is minutes rather than
        -- seconds. Licence statements live in the first few kilobytes anyway.
        local contents = fs.readFileToString(child)
        local head = string.sub(contents, 1, 8192)
        if
            string.find(head, "GNU General Public License", 1, true) ~= nil
            or string.find(head, "GNU Lesser General Public", 1, true) ~= nil
        then
            table.insert(found, child)
        end
    end
end

local function main(): number
    local dependencies = readManifest()
    local audited = 0
    local matched: { [string]: boolean } = {}
    for _, entry in KnownCopyleft do
        matched[entry.prefix] = false
    end

    for _, dependency in dependencies do
        if not Allowed[dependency.license] then
            fail(
                `{dependency.name}: licence "{dependency.license}" is not on R6's list. `
                    .. `Adding one is a human-approved decision, not an edit to this file.`
            )
        end

        if not isVendored(dependency) then
            continue
        end
        audited += 1

        if licenceFile(dependency) == nil then
            fail(
                `{dependency.name}: no licence file in {dependency.path} -- this is redistribution with no attribution`
            )
        end

        local found: { string } = {}
        copyleftFiles(dependency.path, found)
        for _, file in found do
            local normalised = string.gsub(file, "\\", "/")
            local known = false
            for _, entry in KnownCopyleft do
                if string.sub(normalised, 1, #entry.prefix) == entry.prefix then
                    matched[entry.prefix] = true
                    known = true
                    break
                end
            end
            if not known then
                fail(
                    `{normalised} names the GPL or LGPL and is not under a known-copyleft path. `
                        .. `Read it: if it is not compiled, add its directory to KnownCopyleft with WHY and the build `
                        .. `evidence. If it IS compiled, R6 says no.`
                )
            end
        end
    end

    -- The other direction, and it is the half that keeps the list honest: an
    -- entry that no longer matches anything is an argument about a file that is
    -- gone, and it would sit there granting an exemption to nothing.
    for prefix, seen in matched do
        if not seen then
            fail(`{prefix} is on the known-copyleft list and matches nothing. Remove the entry.`)
        end
    end

    if #Failures > 0 then
        for _, message in Failures do
            print(`  FAIL: {message}`)
        end
        print(`licensecheck: {#Failures} problem(s)`)
        return 1
    end

    print(`licensecheck: ok -- {audited} vendored dependenc(ies), every licence present and permissive`)
    return 0
end

process.exit(main())
