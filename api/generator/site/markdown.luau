--!strict
--[[
	The Markdown subset the documentation site is authored in.

	**Why a renderer of our own rather than a dependency.** Rule R5 makes every
	added dependency a human-approved ADR, and the alternative here is a
	JavaScript toolchain -- npm, a bundler and a theme -- to render prose that
	this repository already writes in a dialect it controls. The subset below is
	the one `docs/manual/**` and the IDL doc strings actually use, and anything
	outside it is a page that has to be rewritten rather than a feature that has
	to be implemented.

	**Two inline forms exist here that CommonMark does not have**, and both are
	the reason the site can cross-reference the engine at all:

	- A link whose target begins `api:` resolves against the IDL --
	  `[the shape](api:Part.Shape)`. It is spelled with a colon deliberately:
	  `scripts/gates/docs-lint.sh` skips any link target containing one, exactly
	  as it skips `https:`, so the manual keeps its API links without the
	  documentation gate trying to open them as files.
	- A code span whose contents name a public symbol becomes a link to it
	  automatically. `Part`, `BasePart.Anchored`, `Instance:Destroy`,
	  `Enum.PartShape` and `task.wait` all resolve; anything else stays a plain
	  code span. This is what keeps the doc prose in `api/defs/*.api.luau`
	  navigable without a single link being authored in it.

	Both resolve through the callbacks in `RenderOptions`, so this module knows
	the syntax and nothing about the engine.
]]

local Escapes: { [string]: string } = {
    ["&"] = "&amp;",
    ["<"] = "&lt;",
    [">"] = "&gt;",
    ['"'] = "&quot;",
}

--- Resolves a `[text](target)` whose target this module does not understand,
--- and a code span that might name a symbol. Both return nil for "not mine",
--- which is what lets an unresolved reference stay readable instead of
--- becoming a dead link.
export type RenderOptions = {
    --- Given a link target as authored, the href to emit, or nil to leave the
    --- target untouched.
    resolveLink: ((target: string) -> string?)?,
    --- Given the contents of a code span, the href it names, or nil for a span
    --- that is just code.
    resolveSymbol: ((text: string) -> string?)?,
    --- Prefix applied to a root-relative asset or page path, so a page nested
    --- two directories deep still reaches the stylesheet.
    relativePrefix: string?,
    --- Collects every heading the document declares, in order, so a page can
    --- render its own table of contents.
    headings: { { Level: number, Id: string, Text: string } }?,
}

local function escape(text: string): string
    return (string.gsub(text, '[&<>"]', Escapes))
end

--- The anchor a heading gets, and the same rule `pages.luau` uses for a member
--- anchor, so `#Part.Shape` and a hand-written `## Part.Shape` cannot disagree.
local function slugify(text: string): string
    local slug = string.lower(text)
    slug = string.gsub(slug, "`", "")
    slug = string.gsub(slug, "[^%w%s%-%.]", "")
    slug = string.gsub(slug, "%s+", "-")
    slug = string.gsub(slug, "%-+", "-")
    slug = string.gsub(slug, "^%-", "")
    slug = string.gsub(slug, "%-$", "")
    return slug
end

local function trim(text: string): string
    return (string.match(text, "^%s*(.-)%s*$")) or ""
end

--- Finds the closing run of `marker` starting at `from`, skipping any run that
--- sits inside a code span. Without the skip, a sentence carrying
--- `` `a * b` `` and a real emphasis later in the same line pairs the wrong two.
local function findClosing(text: string, marker: string, from: number): number?
    local length = #marker
    local index = from
    while index <= #text do
        local character = string.sub(text, index, index)
        if character == "`" then
            local closing = string.find(text, "`", index + 1, true)
            if closing == nil then
                return nil
            end
            index = closing + 1
        elseif string.sub(text, index, index + length - 1) == marker then
            return index
        else
            index += 1
        end
    end
    return nil
end

--- Matches `[text](target)` or `![alt](src)` at `start`, returning the two
--- parts and the index just past the closing parenthesis.
---
--- Written as a scanner rather than as a pattern because a link's text may
--- itself contain brackets -- `[the `[]` operator](api:...)` is prose this
--- repository writes -- and Luau patterns have no balanced-bracket construct
--- that also stops at the right place.
local function matchLink(text: string, start: number): (string?, string?, number)
    if string.sub(text, start, start) ~= "[" then
        return nil, nil, start
    end

    local depth = 0
    local index = start
    local labelEnd: number? = nil
    while index <= #text do
        local character = string.sub(text, index, index)
        if character == "[" then
            depth += 1
        elseif character == "]" then
            depth -= 1
            if depth == 0 then
                labelEnd = index
                break
            end
        end
        index += 1
    end

    if labelEnd == nil or string.sub(text, labelEnd + 1, labelEnd + 1) ~= "(" then
        return nil, nil, start
    end

    local targetEnd = string.find(text, ")", labelEnd + 2, true)
    if targetEnd == nil then
        return nil, nil, start
    end

    local label = string.sub(text, start + 1, labelEnd - 1)
    local target = string.sub(text, labelEnd + 2, targetEnd - 1)
    return label, target, targetEnd + 1
end

local inline: (string, RenderOptions) -> string

--- The options a LINK LABEL is rendered with: the same ones, minus symbol
--- auto-linking. A code span inside a label would otherwise become an anchor
--- inside an anchor, which no browser parses as nesting -- it closes the outer
--- one early and leaves the rest of the label outside any link at all.
local function labelOptions(options: RenderOptions): RenderOptions
    if options.resolveSymbol == nil then
        return options
    end
    return {
        resolveLink = options.resolveLink,
        resolveSymbol = nil,
        relativePrefix = options.relativePrefix,
        headings = options.headings,
    }
end

--- A code span, plus the auto-link that makes the reference prose navigable.
local function codeSpan(contents: string, options: RenderOptions): string
    local rendered = `<code>{escape(contents)}</code>`
    local resolve = options.resolveSymbol
    if resolve == nil then
        return rendered
    end
    local href = resolve(contents)
    if href == nil then
        return rendered
    end
    return `<a class="api-ref" href="{escape(href)}">{rendered}</a>`
end

function inline(text: string, options: RenderOptions): string
    local out: { string } = {}
    local plain: { string } = {}

    local function flush()
        if #plain > 0 then
            table.insert(out, escape(table.concat(plain)))
            table.clear(plain)
        end
    end

    local index = 1
    while index <= #text do
        local character = string.sub(text, index, index)

        if character == "`" then
            local closing = string.find(text, "`", index + 1, true)
            if closing ~= nil then
                flush()
                table.insert(out, codeSpan(string.sub(text, index + 1, closing - 1), options))
                index = closing + 1
                continue
            end
        elseif character == "!" and string.sub(text, index + 1, index + 1) == "[" then
            local alt, source, after = matchLink(text, index + 1)
            if alt ~= nil and source ~= nil then
                flush()
                local resolve = options.resolveLink
                local href = if resolve ~= nil then resolve(source) else nil
                table.insert(out, `<img src="{escape(href or source)}" alt="{escape(alt)}">`)
                index = after
                continue
            end
        elseif character == "[" then
            local label, target, after = matchLink(text, index)
            if label ~= nil and target ~= nil then
                flush()
                local resolve = options.resolveLink
                local href = if resolve ~= nil then resolve(target) else nil
                local external = string.match(target, "^https?://") ~= nil
                local attributes = if external then ' target="_blank" rel="noreferrer"' else ""
                table.insert(
                    out,
                    `<a href="{escape(href or target)}"{attributes}>{inline(label, labelOptions(options))}</a>`
                )
                index = after
                continue
            end
        elseif string.sub(text, index, index + 1) == "**" then
            local closing = findClosing(text, "**", index + 2)
            if closing ~= nil then
                flush()
                table.insert(out, `<strong>{inline(string.sub(text, index + 2, closing - 1), options)}</strong>`)
                index = closing + 2
                continue
            end
        elseif character == "*" then
            local closing = findClosing(text, "*", index + 1)
            if closing ~= nil then
                flush()
                table.insert(out, `<em>{inline(string.sub(text, index + 1, closing - 1), options)}</em>`)
                index = closing + 1
                continue
            end
        end

        table.insert(plain, character)
        index += 1
    end

    flush()
    return table.concat(out)
end

--- Splits a pipe-table row into cells, honouring `\|` as an escaped pipe. The
--- IDL's doc prose contains type unions written with pipes, and
--- `gen_reference.luau` escapes them on the way into a Markdown table; reading
--- one back has to undo exactly that.
local function tableCells(line: string): { string }
    local cells: { string } = {}
    local current: { string } = {}
    local index = 1
    while index <= #line do
        local character = string.sub(line, index, index)
        if character == "\\" and string.sub(line, index + 1, index + 1) == "|" then
            table.insert(current, "|")
            index += 2
        elseif character == "|" then
            table.insert(cells, trim(table.concat(current)))
            table.clear(current)
            index += 1
        else
            table.insert(current, character)
            index += 1
        end
    end
    table.insert(cells, trim(table.concat(current)))

    -- A GFM row is written with a leading and a trailing pipe, which produce an
    -- empty cell at each end that is punctuation rather than data.
    if #cells > 0 and cells[1] == "" then
        table.remove(cells, 1)
    end
    if #cells > 0 and cells[#cells] == "" then
        table.remove(cells)
    end
    return cells
end

local function isTableDivider(line: string): boolean
    return string.match(line, "^%s*|?[%s:%-|]+|?%s*$") ~= nil and string.find(line, "%-") ~= nil
end

--- Renders a document. Block structure is decided by a single pass over the
--- lines, which is what a Markdown subset buys: there is no lazy continuation,
--- no setext heading and no nested block quote, so a line's first characters
--- decide what it is.
local function render(source: string, options: RenderOptions): string
    local lines: { string } = string.split(string.gsub(source, "\r\n", "\n"), "\n")
    local out: { string } = {}
    local index = 1

    local function paragraphBreak(): boolean
        local line = lines[index]
        return line == nil or trim(line) == ""
    end

    while index <= #lines do
        local line = lines[index]
        local stripped = trim(line)

        if stripped == "" then
            index += 1
        elseif string.match(stripped, "^```") ~= nil then
            local language = trim(string.sub(stripped, 4))
            local body: { string } = {}
            index += 1
            while index <= #lines and string.match(trim(lines[index]), "^```") == nil do
                table.insert(body, lines[index])
                index += 1
            end
            index += 1
            local class = if language ~= "" then ` class="language-{escape(language)}"` else ""
            table.insert(out, `<pre><code{class}>{escape(table.concat(body, "\n"))}</code></pre>`)
        elseif string.match(stripped, "^#+%s") ~= nil then
            local hashes = string.match(stripped, "^(#+)%s") :: string
            local level = math.min(#hashes, 6)
            local text = trim(string.sub(stripped, #hashes + 1))
            local id = slugify(text)
            if options.headings ~= nil then
                table.insert(options.headings, { Level = level, Id = id, Text = text })
            end
            local anchor = `<a class="anchor" href="#{escape(id)}" aria-label="Link to this section">#</a>`
            table.insert(out, `<h{level} id="{escape(id)}">{inline(text, options)}{anchor}</h{level}>`)
            index += 1
        elseif stripped == "---" or stripped == "***" then
            table.insert(out, "<hr>")
            index += 1
        elseif string.match(stripped, "^>%s?") ~= nil then
            local body: { string } = {}
            while index <= #lines and string.match(trim(lines[index]), "^>") ~= nil do
                table.insert(body, (string.gsub(trim(lines[index]), "^>%s?", "")))
                index += 1
            end
            table.insert(out, `<blockquote>{render(table.concat(body, "\n"), options)}</blockquote>`)
        elseif string.find(stripped, "|") ~= nil and isTableDivider(lines[index + 1] or "") then
            local header = tableCells(stripped)
            index += 2
            local rows: { { string } } = {}
            while index <= #lines and string.find(lines[index], "|") ~= nil and trim(lines[index]) ~= "" do
                table.insert(rows, tableCells(trim(lines[index])))
                index += 1
            end

            local html: { string } = { '<div class="table-scroll"><table><thead><tr>' }
            for _, cell in header do
                table.insert(html, `<th>{inline(cell, options)}</th>`)
            end
            table.insert(html, "</tr></thead><tbody>")
            for _, row in rows do
                table.insert(html, "<tr>")
                for column = 1, #header do
                    table.insert(html, `<td>{inline(row[column] or "", options)}</td>`)
                end
                table.insert(html, "</tr>")
            end
            table.insert(html, "</tbody></table></div>")
            table.insert(out, table.concat(html))
        elseif string.match(stripped, "^[%-%*]%s") ~= nil or string.match(stripped, "^%d+%.%s") ~= nil then
            local ordered = string.match(stripped, "^%d+%.%s") ~= nil
            local items: { string } = {}
            local current: { string }? = nil
            while index <= #lines do
                local candidate = lines[index]
                local candidateStripped = trim(candidate)
                local marker = if ordered
                    then string.match(candidateStripped, "^%d+%.%s")
                    else string.match(candidateStripped, "^[%-%*]%s")
                if marker ~= nil then
                    if current ~= nil then
                        table.insert(items, table.concat(current, "\n"))
                    end
                    current = { trim(string.sub(candidateStripped, #marker + 1)) }
                elseif candidateStripped ~= "" and string.match(candidate, "^%s%s+") ~= nil and current ~= nil then
                    -- An indented continuation line belongs to the item above it.
                    table.insert(current, candidateStripped)
                else
                    break
                end
                index += 1
            end
            if current ~= nil then
                table.insert(items, table.concat(current, "\n"))
            end

            local tag = if ordered then "ol" else "ul"
            local html: { string } = { `<{tag}>` }
            for _, item in items do
                table.insert(html, `<li>{inline(item, options)}</li>`)
            end
            table.insert(html, `</{tag}>`)
            table.insert(out, table.concat(html))
        else
            local body: { string } = {}
            while not paragraphBreak() do
                local candidate = trim(lines[index])
                if
                    string.match(candidate, "^#+%s") ~= nil
                    or string.match(candidate, "^```") ~= nil
                    or string.match(candidate, "^>%s?") ~= nil
                    or string.match(candidate, "^[%-%*]%s") ~= nil
                    or string.match(candidate, "^%d+%.%s") ~= nil
                    or candidate == "---"
                then
                    break
                end
                table.insert(body, candidate)
                index += 1
            end
            if #body > 0 then
                table.insert(out, `<p>{inline(table.concat(body, "\n"), options)}</p>`)
            else
                index += 1
            end
        end
    end

    return table.concat(out, "\n")
end

--- Doc prose from the IDL, which is authored as one or more paragraphs of
--- inline Markdown and never carries a heading, a list or a table.
local function prose(text: string, options: RenderOptions): string
    local paragraphs = string.split(text, "\n\n")
    local out: { string } = {}
    for _, paragraph in paragraphs do
        local trimmed = trim(paragraph)
        if trimmed ~= "" then
            table.insert(out, `<p>{inline((string.gsub(trimmed, "\n", " ")), options)}</p>`)
        end
    end
    return table.concat(out, "\n")
end

--- The first sentence of a doc string, for a summary column or a search index.
--- Splits on a full stop followed by a space, so `0.5` and `§2.2` survive.
local function summarize(text: string): string
    local firstParagraph = string.split(text, "\n\n")[1] or text
    firstParagraph = string.gsub(trim(firstParagraph), "%s+", " ")
    local stop = string.find(firstParagraph, "%. ")
    if stop ~= nil then
        return string.sub(firstParagraph, 1, stop)
    end
    return firstParagraph
end

return {
    escape = escape,
    slugify = slugify,
    trim = trim,
    inline = inline,
    render = render,
    prose = prose,
    summarize = summarize,
}
