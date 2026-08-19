--!strict
--[[
	The only sanctioned way to populate `third_party/` (ADR 0021).

	Contracts this tool exists to enforce:

	  * `third_party/manifest.json` is the sole authority on *what* is vendored
	    and at *which commit*. This tool never writes it. Pinning is a
	    human-approved decision (R5); a vendor tool able to rewrite its own pins
	    would make "pinned" meaningless.
	  * A vendored tree is exact upstream content at the pinned commit with no
	    `.git` directory, so the repository stays hermetic and submodule-free.
	  * Local modifications live only in `third_party/patches/<name>/*.patch`
	    and are re-applied on every import (R13 — a vendored tree is never
	    edited in place).
	  * `THIRD_PARTY_NOTICES.md` is generated from the manifest, never authored.

	Runs on the pinned, unmodified Lute (ADR 0003), from the repository root:

	    lute tools/repo/vendor.luau status
	    lute tools/repo/vendor.luau sync [name...]
	    lute tools/repo/vendor.luau notices
	    lute tools/repo/vendor.luau resolve <name> <ref>

	`resolve` exists so a pin is always read off upstream rather than recalled
	from memory (MASTER_PROMPT.md section 9).
]]

local fs = require("@std/fs")
local json = require("@std/json")
local process = require("@std/process")

local MANIFEST_PATH = "third_party/manifest.json"
local NOTICES_PATH = "THIRD_PARTY_NOTICES.md"
local PATCH_ROOT = "third_party/patches"

-- Rows carrying this placeholder are declared but not yet pinned. The roadmap
-- allows dependencies needed by later milestones to be vendored lazily, so an
-- unpinned row is a skip, not an error.
local UNPINNED = "TBD-AT-M0"

type Dependency = {
	name: string,
	version: string,
	commit: string,
	license: string,
	upstream: string,
	path: string,
	patches: { string },
}

local function fail(message: string): never
	print(`vendor: ERROR: {message}`)
	return process.exit(1)
end

local function info(message: string)
	print(`vendor: {message}`)
end

local function runGit(args: { string }, what: string, cwd: string?): string
	local argv = table.clone(args)
	table.insert(argv, 1, "git")

	local result = process.run(argv, if cwd then { cwd = cwd } else nil)
	if not result.ok then
		fail(`{what} failed (exit {result.exitcode})\n{result.stderr}`)
	end
	return result.stdout
end

local function expectString(row: json.Object, key: string, context: string): string
	local value = row[key]
	if typeof(value) ~= "string" then
		fail(`{context}: field "{key}" must be a string`)
	end
	return value :: string
end

local function readManifest(): { Dependency }
	if not fs.exists(MANIFEST_PATH) then
		fail(`{MANIFEST_PATH} not found — run this from the repository root`)
	end

	local decoded = json.deserialize(fs.readFileToString(MANIFEST_PATH))
	local root = json.asObject(decoded)
	if not root then
		fail(`{MANIFEST_PATH} is not a JSON object`)
	end

	local rows = json.asArray((root :: json.Object).dependencies)
	if not rows then
		fail(`{MANIFEST_PATH}: "dependencies" must be an array`)
	end

	local result: { Dependency } = {}
	for index, entry in rows :: json.Array do
		local row = json.asObject(entry)
		if not row then
			fail(`{MANIFEST_PATH}: dependency #{index} is not an object`)
		end

		local context = `{MANIFEST_PATH} dependency #{index}`
		local patches: { string } = {}
		local declaredPatches = json.asArray((row :: json.Object).patches)
		if declaredPatches then
			for _, patch in declaredPatches do
				if typeof(patch) ~= "string" then
					fail(`{context}: "patches" entries must be strings`)
				end
				table.insert(patches, patch :: string)
			end
		end

		table.insert(result, {
			name = expectString(row :: json.Object, "name", context),
			version = expectString(row :: json.Object, "version", context),
			commit = expectString(row :: json.Object, "commit", context),
			license = expectString(row :: json.Object, "license", context),
			upstream = expectString(row :: json.Object, "upstream", context),
			path = expectString(row :: json.Object, "path", context),
			patches = patches,
		})
	end

	return result
end

local function isPinned(dependency: Dependency): boolean
	return dependency.commit ~= UNPINNED and #dependency.commit == 40
end

-- Vendor scratch (a bare-ish git dir) is build output and must never live in
-- the source tree (R14). It is also what keeps `.git` out of the vendored tree:
-- the fetch happens here, and only the working tree is checked out into place.
local function scratchRoot(): string
	local buildRoot = process.env.LUAUG_BUILD_ROOT
	if not buildRoot or buildRoot == "" then
		fail("LUAUG_BUILD_ROOT is not set — run scripts/bootstrap first (R14)")
	end
	return `{buildRoot}/vendor-scratch`
end

local function applyPatches(dependency: Dependency)
	local dir = `{PATCH_ROOT}/{dependency.name}`
	if #dependency.patches == 0 then
		return
	end

	for _, patch in dependency.patches do
		local patchPath = `{dir}/{patch}`
		if not fs.exists(patchPath) then
			fail(`{dependency.name}: manifest lists patch "{patch}" but {patchPath} is missing`)
		end
		info(`{dependency.name}: applying {patch}`)
		runGit({ "apply", "--whitespace=nowarn", "-p1", `{process.cwd()}/{patchPath}` }, `patch {patch}`, dependency.path)
	end
end

local function syncOne(dependency: Dependency)
	local scratch = `{scratchRoot()}/{dependency.name}`
	local gitDir = `{scratch}/.git`

	if fs.exists(scratch) then
		fs.removeDirectory(scratch, { recursive = true })
	end
	fs.createDirectory(scratch, { makeParents = true })

	if fs.exists(dependency.path) then
		fs.removeDirectory(dependency.path, { recursive = true })
	end
	fs.createDirectory(dependency.path, { makeParents = true })

	info(`{dependency.name}: fetching {dependency.commit} from {dependency.upstream}`)
	runGit({ `--git-dir={gitDir}`, "init", "--quiet" }, `{dependency.name} init`)
	runGit({ `--git-dir={gitDir}`, "remote", "add", "origin", dependency.upstream }, `{dependency.name} remote`)
	runGit({ `--git-dir={gitDir}`, "fetch", "--quiet", "--depth", "1", "origin", dependency.commit }, `{dependency.name} fetch`)

	-- Proves the tree really is the pinned commit rather than whatever the
	-- remote's default branch happened to be.
	local head = string.gsub(runGit({ `--git-dir={gitDir}`, "rev-parse", "FETCH_HEAD" }, `{dependency.name} rev-parse`), "%s+$", "")
	if head ~= dependency.commit then
		fail(`{dependency.name}: fetched {head} but manifest pins {dependency.commit}`)
	end

	runGit({
		`--git-dir={gitDir}`,
		`--work-tree={dependency.path}`,
		"checkout",
		"--quiet",
		"FETCH_HEAD",
		"--",
		":/",
	}, `{dependency.name} checkout`)

	fs.removeDirectory(scratch, { recursive = true })
	applyPatches(dependency)

	info(`{dependency.name}: vendored at {dependency.path}`)
end

local function commandSync(names: { string })
	local dependencies = readManifest()
	local selected: { Dependency } = {}

	if #names == 0 then
		for _, dependency in dependencies do
			if isPinned(dependency) then
				table.insert(selected, dependency)
			else
				info(`{dependency.name}: skipped — not pinned yet (commit is "{dependency.commit}")`)
			end
		end
	else
		for _, name in names do
			local found: Dependency? = nil
			for _, dependency in dependencies do
				if dependency.name == name then
					found = dependency
					break
				end
			end
			if not found then
				fail(`no dependency named "{name}" in {MANIFEST_PATH}`)
			end
			if not isPinned(found :: Dependency) then
				fail(`{name}: not pinned — set a real commit SHA in {MANIFEST_PATH} first (R5)`)
			end
			table.insert(selected, found :: Dependency)
		end
	end

	if #selected == 0 then
		info("nothing to do — no pinned dependencies")
		return
	end

	for _, dependency in selected do
		syncOne(dependency)
	end

	info(`{#selected} dependenc{if #selected == 1 then "y" else "ies"} vendored`)
	-- Upstream trees carry their own .gitignore files; without -f, git would
	-- silently drop vendored files that upstream ignores.
	info("stage with: git add -f third_party")
end

local function commandStatus()
	local dependencies = readManifest()
	local missing = 0
	local unpinned = 0

	for _, dependency in dependencies do
		local present = fs.exists(dependency.path) and fs.type(dependency.path) == "dir"
		local hasContent = present and #fs.listDirectory(dependency.path) > 0

		local state: string
		if not isPinned(dependency) then
			state = "unpinned"
			unpinned += 1
		elseif not hasContent then
			state = "MISSING"
			missing += 1
		else
			state = `ok {string.sub(dependency.commit, 1, 12)}`
		end

		print(string.format("  %-24s %-12s %s", dependency.name, dependency.version, state))
	end

	print(`vendor: {#dependencies} rows, {missing} pinned-but-missing, {unpinned} unpinned`)
	if missing > 0 then
		process.exit(1)
	end
end

local function commandNotices()
	local dependencies = readManifest()
	local lines = {
		"# Third-Party Notices",
		"",
		"GENERATED FILE — do not edit. Regenerate with:",
		"",
		"```",
		"lute tools/repo/vendor.luau notices",
		"```",
		"",
		"LuauG vendors third-party source under `third_party/`. Each dependency keeps",
		"its own license; this file is the aggregated index, generated from",
		"[`third_party/manifest.json`](third_party/manifest.json) (ADR 0021).",
		"",
		"Rows without a pinned commit are declared for a later milestone and are not",
		"vendored yet; the roadmap allows lazy vendoring so long as the row exists.",
		"",
		"| Dependency | Version | Commit | License | Upstream | Vendored path |",
		"|---|---|---|---|---|---|",
	}

	for _, dependency in dependencies do
		local commit = if isPinned(dependency) then `\`{string.sub(dependency.commit, 1, 12)}\`` else "_not vendored yet_"
		table.insert(
			lines,
			`| {dependency.name} | {dependency.version} | {commit} | {dependency.license} | {dependency.upstream} | \`{dependency.path}/\` |`
		)
	end

	table.insert(lines, "")
	table.insert(lines, "Policy: permissive licenses only (MIT / BSD / zlib / Apache-2.0 / public domain).")
	table.insert(lines, "No GPL, no LGPL, and no commercial dependency in the default build path. Adding")
	table.insert(lines, "or upgrading a dependency requires human approval (see `MASTER_PROMPT.md`,")
	table.insert(lines, "escalation rules) and an ADR.")
	table.insert(lines, "")
	table.insert(lines, "Tooling installed via rokit (not vendored): Lute 1.0.0 (MIT), luau-lsp 1.69.0")
	table.insert(lines, "(MIT), StyLua 2.5.2 (MPL-2.0 — a build tool, never linked into the engine or")
	table.insert(lines, "shipped games).")
	table.insert(lines, "")

	fs.writeStringToFile(NOTICES_PATH, table.concat(lines, "\n"))
	info(`wrote {NOTICES_PATH} from {#dependencies} manifest rows`)
end

local function commandResolve(name: string, ref: string)
	local dependencies = readManifest()
	for _, dependency in dependencies do
		if dependency.name == name then
			local output = runGit({ "ls-remote", dependency.upstream, ref, ref .. "^{}" }, `resolve {name}`)
			if output == "" then
				fail(`{name}: upstream has no ref matching "{ref}"`)
			end
			-- An annotated tag resolves to two lines; the dereferenced `^{}`
			-- entry is the commit the pin must record.
			local resolved: string? = nil
			for line in string.gmatch(output, "[^\r\n]+") do
				local sha, refName = string.match(line, "^(%x+)%s+(.+)$")
				if sha and refName then
					if string.sub(refName, -3) == "^{}" or not resolved then
						resolved = sha
					end
				end
			end
			if not resolved then
				fail(`{name}: could not parse ls-remote output for "{ref}"`)
			end
			print(resolved)
			return
		end
	end
	fail(`no dependency named "{name}" in {MANIFEST_PATH}`)
end

local function main()
	-- Lute passes the script path as args[1]; user arguments start at 2.
	local args = process.args
	local command = args[2]

	if command == "sync" then
		local names: { string } = {}
		for index = 3, #args do
			table.insert(names, args[index])
		end
		commandSync(names)
	elseif command == "status" then
		commandStatus()
	elseif command == "notices" then
		commandNotices()
	elseif command == "resolve" then
		if not args[3] or not args[4] then
			fail("usage: vendor.luau resolve <name> <ref>")
		end
		commandResolve(args[3], args[4])
	else
		fail("usage: vendor.luau <sync [name...] | status | notices | resolve <name> <ref>>")
	end
end

main()
