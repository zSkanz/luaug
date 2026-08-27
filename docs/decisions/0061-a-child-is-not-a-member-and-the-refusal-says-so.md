# 0061 — A child is not a member, and the refusal says so in three places

- Status: accepted
- Date: 2026-08-27
- Confirms: `api-design.md` divergence #26 (dot-access to children)
- Relates to: [0018](0018-strict-luau-new-solver.md) (strict Luau, new solver)

## Context

`workspace.Baseplate` does not reach a child in this engine. It raises. That has
been true since the API was designed and is written down as divergence #26, and
it is the single most-reported piece of friction the owner has raised — twice,
in their own words, about their own project.

A divergence note is not a decision record. The note explains the reasoning to
somebody who already agrees; what it does not do is state the **price of
reversing it**, which is the thing a future reader actually needs. This record
exists so that turning dot-access on later is reversing a decision rather than
discovering one.

## The price, stated in the terms that matter

The divergence note says child access would be "untypeable" and that every such
access would be `any`. That undersells it, and the undersell is what makes the
decision look closer than it is.

Reaching a child with a dot requires a **string indexer on `Instance`**:

```luau
declare class Instance
    [string]: Instance?    -- what `workspace.Baseplate` would need
end
```

An indexer does not only type the child access. It makes **every unknown key on
every instance** resolve to `Instance?` instead of erroring. So:

```luau
part.Positon = Vector3.new(1, 2, 3)   -- today: a type error
                                      -- with an indexer: a silent nil write
```

The cost is not "child access is weakly typed". The cost is **typo detection
across the whole language**, in a repository where R2 makes every file strict and
where the analyzer is the first thing a beginner meets. A misspelled property is
the most common mistake anybody makes in this API, and it would stop being
caught.

The familiarity argument is also weaker than it looks. The platform this
borrows its shape from flags the same expression under its own strict mode, so
what would be imported is a habit that is **non-strict there too** — not a
convention this engine is refusing, but one that platform's own type system
also declines to bless.

## Decision

**Keep the divergence.** A child is reached by `FindFirstChild` or
`WaitForChild`, and members and children stay in separate namespaces.

**And stop the refusal being silent**, because what was reported twice was
almost certainly not "give me the indexer" — it was *this fails and tells me
nothing*. Three instruments, one fact:

1. **At run time.** `scene.err.child_not_member` replaces the bare
   `scene.err.unknown_member` when the name IS a live child: *"Folder has no
   member named "Nested", but it has a child called that. Children are not
   reached by dot access: use FindFirstChild("Nested")."* A name that is nothing
   at all still gets the plain message — a single message for both would
   recommend `FindFirstChild` for `part.Positon`, which is worse than saying
   nothing.

2. **At edit time.** `lintInstanceAccess` underlines the access while somebody
   is typing. It resolves the path first, so every report is a fact: that
   instance exists, it has that child, and its class has no such member. It
   lives in `script_complete.cpp` rather than in the AST lint pass because it
   needs the **tree** — without one, `t.Baseplate` on a plain table cannot be
   told from `workspace.Baseplate` on the service, and a lint with false
   positives is a lint people turn off.

3. **In the completion.** It used to offer children under a dot, which was the
   inconsistency: the editor proposed a line the runtime raises on. Children are
   now offered inside `WaitForChild("` and `FindFirstChild("`, where they can be
   typed.

**`FindFirstChild` is what all three recommend, not `WaitForChild`.** That is
not a style preference. Scripts start when play starts and the tree is already
built, so recommending the yielding one would teach exactly the load-order habit
the divergence exists to kill — the habit that made `WaitForChild` load-bearing
on the platform this borrows from.

## Consequences

- The most-reported friction in the API now explains itself at the moment it
  bites, three times over, instead of answering with a true and useless
  sentence.
- `luaug_app_tests` holds the completion's half and the lint's, including four
  cases that assert **silence** — a declared member, a member with a child of
  the same name, a misspelled property, and a plain table. Half of a lint's
  value is what it does not say.
- `tests/conformance/instance/dot_access_children.spec.luau` holds the runtime's
  half, including the case that must keep the plain message.
- **Reversing this is now one decision with a stated price.** If a future
  milestone wants the indexer, what it is buying is familiarity and what it is
  spending is typo detection on every property write in every strict file. That
  trade is legible here; it was not legible in a table row.
