--!strict
--[[
	Turns the API definition's doc prose into prose for somebody USING the
	engine.

	**Why a transformation exists at all.** `api/defs/*.api.luau` is the
	engine's own specification, and its doc strings are written for the people
	maintaining it: they cite the design document by section number, and they
	cite the decision records that settled a behaviour. Those citations are
	correct and they are useful to a maintainer. To a reader who has the engine
	and not the repository, `(§2.4, ADR 0029)` is a reference to a document they
	do not have, attached to a sentence that was complete without it.

	So the published site drops them. It does not paraphrase, soften, or
	summarise anything else -- the sentence a maintainer reads and the sentence
	a user reads are the same sentence, minus a citation that only resolves
	inside the repository.

	**What is deliberately NOT done here.** A citation whose surrounding
	sentence would stop making sense without it is not something a regular
	expression can fix, and pretending otherwise produces prose with a hole in
	it. Those are fixed at the source instead, in the definition files, where
	the sentence can be rewritten properly. This module handles the mechanical
	case only, and `check` below is what proves the mechanical case is the only
	one left.
]]

--- A parenthetical that consists ENTIRELY of citations is removed with its
--- parentheses and the space before it. The list is ordered longest-first
--- because `§2.5, divergence #11` must not be matched as `§2.5` with a
--- trailing fragment left behind.
local ParentheticalPatterns: { string } = {
    "%s*%(api%-design%.md §[%d%.]+[^%)]*%)",
    "%s*%(§[%d%.]+,%s*ADR %d%d%d%d%)",
    "%s*%(§[%d%.]+,%s*divergence[^%)]*%)",
    "%s*%(§[%d%.]+,%s*§[%d%.]+%)",
    "%s*%(§[%d%.]+[%s,]+[a-z][^%)%.]*%)",
    "%s*%(§[%d%.]+%)",
    "%s*%(ADR %d%d%d%d,%s*ADR %d%d%d%d%)",
    "%s*%(ADR %d%d%d%d%)",
    "%s*%(roadmap [^%)]*%)",
    "%s*%(rule R%d+%)",
    "%s*%(R%d+%)",
    "%s*%(R%d+,%s*[^%)]*%)",
}

--- A citation that is not the whole parenthetical loses only itself, and the
--- punctuation that introduced it. `(ADR 0039's sibling argument: ...)` keeps
--- the argument and drops the attribution, because the argument is the part
--- worth reading.
local InlinePatterns: { { Find: string, Replace: string } } = {
    { Find = "ADR %d%d%d%d's sibling argument: ", Replace = "" },
    { Find = "ADR %d%d%d%d addendum, ADR %d%d%d%d", Replace = "" },
    { Find = "%s*%(§[%d%.]+'s [^%)]*%)", Replace = "" },
    { Find = "%s*%(§[%d%.]+ divergence %d+%)", Replace = "" },
}

--- Anything still matching these after the passes above is a citation the
--- mechanical rules could not remove, which means the SENTENCE has to change.
--- `gen_site` treats a hit as a failed run rather than publishing it.
---
--- A LIST rather than one alternation, because Luau patterns have no `|` --
--- a single pattern spelling three alternatives matches the pipe literally and
--- therefore matches nothing, which is a checker that passes everything.
local ResidualPatterns: { string } = {
    "§[%d%.]+",
    "ADR %d%d%d%d",
    "api%-design%.md",
}

--- Internal vocabulary that names how the engine was built rather than what it
--- does. A sentence containing one of these cannot be repaired by deleting a
--- fragment -- "a HUD button M6's own gate could not reach" needs rewriting --
--- so these are reported, never silently edited.
local ProcessWords: { string } = {
    "%f[%w]M[0-9]%f[%W]",
    "%f[%w]milestone",
    "%f[%w]kickoff",
    "%f[%w]roadmap",
    "%f[%w]replay gate",
    "%f[%w]own gate",
    "brief, Decision",
    "%f[%w]R%d%d%f[%W]",
}

local function clean(text: string): string
    local out = text

    for _, pattern in ParentheticalPatterns do
        out = string.gsub(out, pattern, "")
    end
    for _, rule in InlinePatterns do
        out = string.gsub(out, rule.Find, rule.Replace)
    end

    -- A removed parenthetical can leave a doubled space or a space before a
    -- full stop, and both are visible on a rendered page.
    out = string.gsub(out, "%s+([%.,;:])", "%1")
    out = string.gsub(out, "  +", " ")

    return out
end

--- Every reason this string is not publishable, or an empty list.
local function check(text: string): { string }
    local problems: { string } = {}

    for _, pattern in ResidualPatterns do
        local citation = string.match(text, pattern)
        if citation ~= nil then
            table.insert(problems, `an unremovable citation to {citation}`)
        end
    end
    for _, pattern in ProcessWords do
        local hit = string.match(text, pattern)
        if hit ~= nil then
            table.insert(problems, `the internal term "{hit}"`)
        end
    end
    if string.find(text, "''", 1, true) ~= nil then
        table.insert(problems, "a doubled apostrophe, which renders literally")
    end

    return problems
end

return {
    clean = clean,
    check = check,
}
