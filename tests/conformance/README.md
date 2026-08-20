# Conformance specs

903 cases in 46 files, written against [`docs/api-design.md`](../../docs/api-design.md)
alone by authors who were forbidden from reading `engine/` (MASTER_PROMPT §7).
That is what makes them a contract rather than a transcript of the
implementation, and it is why they were written before the implementation was.

They are the M2 gate. Run by CTest as `conformance`, and by hand with:

```
luaug-host --run-tests=tests/conformance --rhi=null
```

`--rhi=null` because the suite tests the kernel and not the renderer: a machine
with no GPU should still be able to prove the engine's semantics.

## What they cover

`instance/` tree semantics incl. ADR 0026 duplicate names · `signals/`
api-design §3.1's ordering contract · `task/` §3.2 and the removed globals ·
`world/` attributes, tags, services and the script environment · `datatypes/`
Vector3, CFrame, Color3, Random and Enum.

## What integrating them cost, and what it bought

They were checked in with a `.spec.luau.staged` extension and roughly 1,900
`luau-analyze` diagnostics, of which about 1,500 were one mechanical rename:
they were written against camelCase matchers, and ADR 0034 landed afterwards, so
`@luaug/testing` now exports `testing.expect(v):ToBe(x)` and `.Never`. The rest
were real, and split three ways.

**Twelve engine defects.** The specs were right and the code was wrong, and none
of these were found by the C++ tests written alongside the code — which is the
argument for the whole exercise. Among them: `clone` copied `Parent` as though it
were a value; `destroy` left descendants parented to the victim; renaming a child
back to a duplicated name put it *last* in the name chain instead of first, so
`FindFirstChild` answered a different instance than ADR 0026 says; and
`GetAttribute` coerced a non-string key through `luaL_checklstring` instead of
rejecting it.

**Five spec bugs**, corrected against the document, plus three the brief had
already ruled on before the suite ran:

- **R-A**: a property write with an equal value enqueues *nothing*.
- **R-B**: the re-entrancy cap covers `task.defer`.
- **R-D**: the removed globals are tested from C++, not from Luau. Those cases
  were **dropped**; `engine/script/tests/sandbox_tests.cpp` covers that ground.

**Two documentation defects**, where the specs asserted what the document said
and the document was wrong about the VM it describes:

- `v.X` cannot be made to raise. `LOP_GETTABLEKS` answers single-character
  vector indices inline and case-insensitively, before any metatable
  (`lvmexecute.cpp:619`). api-design §2.3 claimed otherwise; U-52 records it.
- `typeof(Enum.PartShape)` cannot be `"Enum"` while `Enum` is a table
  (`ltm.cpp:167` excludes tables from `__type`). The enum objects became tagged
  userdata; U-53 records it.

## Where they are still thin

The suite is a contract, not a proof, and it has holes. One is worth naming
because it was found the expensive way: every `task.delay` case waited a handful
of ticks, so none of them noticed that a forty-tick delay lost its arguments
entirely — the values were parked in storage the deferred drain recycles.
`examples/01-instances` found it instead. A case that holds a claim across many
ticks is worth more than three that hold it across one.
