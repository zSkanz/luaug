# Conformance specs — staged, not yet integrated

932 cases in 47 files, written against [`docs/api-design.md`](../../docs/api-design.md)
alone by authors who were forbidden from reading `engine/` (MASTER_PROMPT §7).
That is what makes them a contract rather than a transcript of the
implementation, and it is why they exist before the implementation does.

They carry a **`.spec.luau.staged`** extension so the analyzer's `*.luau` glob
skips them. That is deliberate and temporary: they are checked in because 340 KB
of specification work should not live in a scratch directory, and they are not
`.luau` yet because they do not pass `luau-analyze` — which the M2 gate requires
of every spec file.

## What integrating them needs

1. **Rename the matchers.** They were written against `describe`/`it`/`expect`
   with camelCase matchers; ADR 0034 landed afterwards and `@luaug/testing` now
   exports `testing.expect(v):ToBe(x)` and `.Never`. Roughly 1,500 of the ~1,900
   current diagnostics are this, and it is a mechanical rename.
2. **Correct the specs that guessed wrong.** Three rulings in
   [`docs/briefs/m2-kickoff.md`](../../docs/briefs/m2-kickoff.md) overrode their
   author's assumption, so the specs assert the opposite of the settled
   behaviour:
   - **R-A**: a property write with an equal value enqueues *nothing*. One area
     asserts an unconditional enqueue.
   - **R-B**: the re-entrancy cap covers `task.defer`. `defer.spec` asserts a
     twelve-generation chain completes, which under the ruling it must not.
   - **R-D**: the removed globals are tested from C++, not from Luau. The
     `removed_globals` and `globals` removal cases are **dropped**;
     `engine/script/tests/sandbox_tests.cpp` covers that ground.
3. **Fill the remaining gaps in the generated definitions.** Some diagnostics
   are real: `CFrame * CFrame` has no operator in `runtime/types/engine.d.luau`
   because the IDL has nowhere to declare one, and datatype metamethods are the
   same gap. That is a schema question, not a spec bug.
4. **Then rename to `.spec.luau`** and they join the gate.

## What they cover

`instance/` tree semantics incl. ADR 0026 duplicate names · `signals/`
api-design §3.1's ordering contract · `task/` §3.2 and the removed globals ·
`world/` attributes, tags, services and the script environment · `datatypes/`
Vector3, CFrame, Color3, Random and Enum.
