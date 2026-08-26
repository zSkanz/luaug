# 0060 — A material is an instance, a stamp is how one is shared, and a trace moves when the hash's inputs do

- Status: accepted
- Date: 2026-08-26
- Reverses: the material-as-asset design built during E9, which had no record of
  its own
- Extends: [0048](0048-content-is-the-source-and-an-instance-is-a-link-to-it.md)
  (content is the source), [0049](0049-a-stamp-is-a-source-and-an-instance-carries-its-mark.md)
  (a stamp is a source), [0051](0051-a-prefab-is-inherited-and-an-edit-is-an-override.md)
  (a prefab is inherited, an edit is an override)
- Relates to: [0025](0025-determinism-guarantee.md) (the determinism guarantee)

## Context

Two things were settled during E9 and written down nowhere. Both are recoverable
from the source and from commit messages, which is the reason this record exists:
a decision that lives in a commit message is a decision the next person
re-debates, because nobody reads a log to find out what the rules are.

### The material was designed twice

E9's plan specified **materials as assets**, in step A4 and again in step 3 of
its order of work. Concretely that meant a new `engine/asset/material.{h,cpp}` —
the existing `MaterialDef` with its four texture indices replaced by content URNs
— a `.material.json` written through `core::JsonWriter` in fixed key order so the
text was a pure function of the struct, `AssetKind::Material` in the pack's table
of contents, `ContentKind::Material` swept through the browser's exhaustive
switches so that `-Werror` would catch a missed arm, and `BasePart.Material` as a
`Content` naming that file.

It was built. `fe979d04` landed the format and both enum values; `6b5d7717`
landed the property and made `Color` a multiplier on whatever surface it found.
Six hours after the first and four after the second, `8ea7eee7` deleted the
format: `asset/material.{h,cpp}`, its tests, `ContentKind::Material` and its
arms, and the content browser's write-a-material-file verb. Its message says why,
and says on whose instruction:

> This replaces the design of the last few commits, on the human's decision, and
> it is a better one. […] All of that was a second answer to questions the stamp
> system already answers: one file, many instances, edit the file and every
> instance follows, override one and only that one differs, see it in the
> Explorer, select it, undo it.

**The argument is recoverable; the conversation behind it is not.** The commit
records the decision as the human's and states the reasoning in the agent's
words, and nothing in the tree distinguishes their argument from a reconstruction
of it. This record does not pretend to know which it was. What it can establish is
that the argument holds against the code: ADRs 0049 and 0051 had already built one
file backing many instances, inheritance from that file, per-instance overrides,
and a browser that places and duplicates — and the deleted design was a second
implementation of every one of those, keyed by URN instead of by mark.

### Two traces moved that the plan said would not move

The same plan's table of what moves goldens carries one row reading
`tests/determinism/{churn,character}` — **must not move** — verify before
landing, not after. Its Part B states the same thing as a hard constraint and
gives the reason: *add no field to an existing component, because new classes
only exist in scenes that create them.*

`churn`'s two traces moved at `6b5d7717` and again at `8ea7eee7`, and were
re-recorded on Windows and in the Tier-2 container both times, justified in the
commit message and nowhere else.

## Decision

### A material is an instance, and there is no material file format

A `Material` is an ordinary instance with an ordinary component
(`MaterialComponent`, `engine/scene/include/luaug/scene/components.h`). It is
created in the world, edited in the Properties grid, selected in the Explorer,
and undone like anything else, because it is not a special case of anything.

**A project keeps one on disk as a stamp**, and that is the whole decision rather
than an implementation note. Everything a shared material wants is what ADR 0051
already decided a stamp does: one file behind many instances, an edit to the file
reaching every instance that has not said otherwise, an override that belongs to
one instance, and a browser row that places one. A `.material.json` would have
been a second answer to each of those, with its own reader, its own writer, its
own library keyed by URN, and its own create-a-file verb — four mechanisms that
have to be kept in agreement with the four that already exist.

### A part names a material by reference, not by path

`BasePart.Material` is an `Instance` property whose `instanceClass` is
`Material`. It names a live instance in the same world, so pointing two parts at
one material is the ordinary meaning of two references to one object, and
dropping a material stamp into the world produces the instance to point at.

`Color` multiplies whatever the reference resolves to, white being the identity,
which is what makes a part with no material and a white part with a white
material draw identically. **There is no texture property on a part and there
will not be one**: a texture reaches a surface through a material or not at all,
which is what makes two parts able to share one and one edit change both. Both
halves of that survived the reversal unchanged — they were right under either
design.

### Its maps are Content URNs and not instances

A map names a source image file. A class whose only content is *which file* would
be an instance standing in for a string, and it would put a second addressing
scheme between a material and its pixels. The line is drawn where a thing has
fields somebody edits: a material does, an image does not.

### A plan states which traces it expects to move; it may not promise one will not

`World::worldHash` walks every declared property through the same generated
accessors a script would use, and hashes the property's **name** as text before
its value (`engine/scene/src/world_hash.cpp`). So **adding a property to an
existing class moves the hash of every world holding an instance of that class,
whether or not anybody ever sets it** — and `hashValue` writes the `ValueType`
tag before the payload, so *retyping* an existing property moves it again even
when the value is empty on both sides.

That makes the plan's constraint two different claims wearing one sentence. Part
B's is true: a **new class** is only in the hash of a scene that creates one, and
`churn` creates none of E9's. The generalisation to the whole milestone is false,
and was already false when it was written, because step A4 put a new property on
`BasePart` and `churn` builds nothing but `Part`s.

So: **a plan lists the scenarios a change is expected to move and why, and never
promises that one will hold still.** The hash decides that, and the only question
a plan can usefully ask is whether the change causing the move is intentional —
which is the standard `tests/determinism/README.md` already sets for
re-recording, and which both commits met by naming the semantic change in the
message that carried the new bytes.

## Consequences

**A material is an instance, it is shared by being a stamp, and a reference to
one is an instance-valued property.** Those three sentences are the shipped
design, and they are coupled: the third is only useful because the second gives a
reference something to name across sessions.

**Instance-valued properties inside a placed stamp are dropped on save**, and
this decision is what turned that from harmless into a loss. ADR 0051 decided
that an instance-valued property is never an override, with a sound reason — the
live one names an instance in the live world and the reference names one in the
stamp's own, so the two ids are not comparable — and `collectOverrides`
implements it by skipping `ValueType::Instance` outright
(`engine/scene/src/scene_file.cpp`). At the time nothing important was
instance-valued. `BasePart.Material` is: point a part inside a placed lamp post at
a material, save, and the scene records nothing, the next open shows an untextured
part, and no count and no message says so. **The exclusion was written for a world
with no instance-valued property worth keeping, and this record put one in it.**
It is an open defect, and the campaign that wrote this record is fixing it.

The stamp-editing half of the same problem was found first and is closed: D133
covers a material placed *beside* the stamp being edited rather than under it,
where the file genuinely cannot carry the reference and the save now counts it
and reports failure instead of writing `null` and pushing that null into every
instance in the world. The placed-stamp half is the mirror image — the reference
is perfectly expressible, and it is discarded by a rule rather than by a limit.

**The property grid's instance-reference editor became load-bearing, and was
inert.** D130 is that: `editable()` returned false for every
`EditorKind::InstanceRef` and had since M4, deliberately, so the picker came up
disabled and the drop target was never installed — a property whose entire
purpose is to be set could not be set by any means, and every test reached the
command behind the widget rather than the predicate that decides whether the
widget is live. D134 is the same design's second bill: dropping a material on a
part that already wears it recorded an undo step that undid nothing. Both are
fixed. Both are the cost of a decision that made one property the first real user
of a surface that had been decorative since M4.

**`AssetKind::Material = 6` survives the design that needed it, with no writer.**
It is in `engine/asset/include/luaug/asset/pack.h` with a comment explaining why a
compiled material deserves its own kind, it round-trips through `assetKindName`
and `assetKindNamed`, and nothing in the engine or in `assetc` ever calls
`PackWriter::add` with it — so no pack or manifest this engine has ever written
contains a row of that kind. It is exactly the inert surface this repository's
gates exist to catch, and it survived because there was no document for anyone to
notice it in. Naming it here is half the remedy; it is either given a writer or
removed, and not left in a third state.

**The sRGB fix does not reach the design that replaced it.** `assetc` decides
whether an image is colour or data from what references it *inside the model being
compiled* — base colour and emissive are colour, normal and metallic-roughness are
numbers (`tools/assetc/src/compiler.cpp`) — and a loose image compiled on its own
is encoded as sRGB unconditionally, under a comment saying there is "no material
to say what it is for". Under this design the material that would have said lives
in a scene, which `assetc` does not read. So every normal and ORM map that reaches
a surface through a `Material` instance rather than through an imported glTF is
bent by a transfer curve, which is the regression `df03f5b0` already closed once
for the other path. That is open, and it is a consequence of this decision rather
than an accident beside it.

**Both designs were paid for.** The IDL, the API dump, the generated class
descriptors, the `.d.luau` types, the documentation page and all four determinism
traces were written twice, once for each. That is the honest cost of reversing a
design four hours after it shipped, and it is smaller than the cost of keeping two
answers to one question.

**`character` could not have moved, and its half of the constraint was vacuous.**
It has carried no committed trace since `4cf3fc26`, which made it `sameBuildOnly`
after CI's Windows runner diverged from a trace recorded by a different compiler:
a physics scenario is floating point amplifying over ticks, and ADR 0025's
guarantee is *same build*. Such a scenario runs three times and requires the three
to agree. A plan naming it beside `churn` was naming two different instruments as
if they were one.

### Rejected

**Keeping `.material.json` beside the instance.** Two ways to have a material
means two ways to share one, two things a picker has to offer, and a question
nobody can answer while looking at a wrong-coloured wall: *which of these two is
the one my part is using?*

**Making a material's maps instances as well.** A texture is a file and nothing
else; a class holding one string would be an instance standing in for it, and the
Explorer would fill with rows nobody edits.

**Refusing the reversal because the first design had already shipped.** It shipped
six hours earlier, into a repository with one writer and no release riding on it.
The cost of the reversal is listed above and it is bounded; the cost of two
material systems is not.

**Re-recording the traces without naming what moved them.**
`tests/determinism/README.md` already refuses this — *`--record-replay` is the
wrong answer to a failure unless you can say which semantic change moved the hash
and why it was intentional* — and both commits met it. The failure was in the
plan's promise, not in the re-recording.

**Freezing the property set so a trace cannot move.** That is a rule against
adding properties, which is a rule against the engine growing. A trace exists to
make a hash change visible and reviewable, not to make it impossible.
