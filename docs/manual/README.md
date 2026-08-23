# docs/manual — the authored half of the documentation

This tree is the manual: the pages a person reads to learn the engine. The other
half — a page for every class, datatype, enum and library — is **generated** from
`api/defs/*.api.luau`, and the two are published together as one site.

```
scripts\docs.ps1              build the site and open it
./scripts/docs.sh             the same
lute api/generator/gen_site.luau [--out=DIR]
```

With no `--out`, the site is written to `$LUAUG_BUILD_ROOT/docs-site`, and to
`out/docs-site` when that variable is unset. It is **not** checked in: it is
built from this tree and the API definition on demand, so a stale copy cannot
exist.

## Why the split

A catalogue of members can be generated, because the definition files know every
member. A paragraph explaining why a checkpoint is a tag rather than a class
cannot be, because nothing in those files knows what a checkpoint is.

So the reference is generated and never edited, and the manual is written and
never generated.

## Adding a page

1. Write `docs/manual/<section>/<slug>.md`, starting with a `# Title` line.
   The title on the page comes from the navigation, not from the file, so one
   authority names it.
2. Add it to `api/generator/site/nav.luau`, in the position it should read in.

A page the navigation declares and nobody wrote **fails the build**. A page
written and not declared is not published — the navigation is the table of
contents, and a table of contents assembled by a directory scan is in whatever
order the filesystem returns.

## Linking

Four forms, and no others. Each carries a colon, which is also what keeps the
documentation gate from resolving it as a file path:

| Form | Goes to |
|---|---|
| `[text](api:Part.Shape)` | A class, member, datatype, enum or library. |
| `[text](manual:physics/bodies)` | Another manual page. |
| `[text](site:reference)` | `site:reference`, `site:hierarchy`, `site:globals`, `site:home`. |
| `[text](https://…)` | Anywhere else. |

**Every one of them is resolved at build time**, and one that does not resolve
fails the run. A manual page cannot go quietly stale against an engine that
renamed a member.

There is also no need to link the API by hand in most cases: a **code span
naming a public symbol becomes a link automatically**. `` `BasePart.Anchored` ``,
`` `Instance:Destroy` ``, `` `Enum.PartShape` `` and `` `task.wait` `` all
resolve.

## Writing

Prefer the shortest true sentence. The house style is the engine's own: say what
a thing is, say what it costs, and say why when the why is the part somebody
will otherwise get wrong.

Two rules that are not style:

- **Do not publish what the engine does not do.** If a property is stored and
  not yet acted on, say so. The reference marks those; the manual should not
  contradict it.
- **Do not name the engine's own development.** No milestones, no decision
  records, no design-document sections, no defect numbers. The site audits for
  those and fails on them, including in the generated prose.

## Code samples

A reference page's sample is a real Luau file under `docs/samples/`, named after
the symbol it demonstrates. See that directory's own README.

## Markdown subset

Headings, paragraphs, `**bold**`, `*italic*`, code spans, fenced code blocks
with a language, links, images, unordered and ordered lists, pipe tables,
blockquotes and horizontal rules.

Anything outside that is a page to rewrite rather than a feature to implement.
Fenced blocks tagged `luau` are syntax-highlighted.
