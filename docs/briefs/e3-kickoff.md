# E3 — Content and Prefabs

**Written at the CLOSE, not at the start, and that is the first thing to know
about it.** E3 had no kickoff: it was specified by the human in a conversation,
one message at a time, while the thing was being built and used. This file is
the record that would have been the brief, assembled from the ADRs, the register
and the log — so read it as an account rather than as a plan that was followed.

Opened 2026-08-23. Signed off 2026-08-23.

## Goal

**Content holds sources, the world holds a world, and an instance in the world
may be a link to a source.** ADR 0048 wrote that model down at the end of E2 in
the human's own words; E3 built it, and reversed two of its own decisions doing
so.

## What was built

**A prefab is a Stamp** (ADR 0049), named by the human over prefab, blueprint
and model. A stamp file is a scene of one subtree — the same writer, the same
reader, a different root — so there is one format rather than two definitions of
"everything about a subtree".

- **Convert any instance to one**, from the row it is on. The instance becomes
  an instance of the file it just made: a source plus a copy of it that nothing
  connects is two things that drift apart by tomorrow.
- **Place one linked or as a copy**, from the browser and from code
  (`Instance.stamp(name)` / `Instance.stamp(name, false)`). Both are real things
  to want: a lamp post you will place forty of wants the link, a starting point
  you are about to rebuild does not.
- **Open one onto a stage** — a `scene::World` of its own with a Workspace, a
  Lighting and nothing else. The game's world is never touched, so there is
  nothing to restore and nothing that can go wrong on the way back.
- **Editing an instance is an OVERRIDE and keeps the link** (ADR 0051).
  Changing the source moves every instance that has not overridden that
  property. A structural change is written in full and unlinked rather than
  refused, because a save that refuses is a save that loses work.
- **The Explorer badges the root**, two draws with a knockout, geometry read
  from the theme.

**A script is an ordinary instance** (ADR 0050) carrying its own `Source`, with
`Script` that runs and `ModuleScript` that is required. `require` takes an
instance. There is no New Script dialog, because there is no file to write.

**And the editor grew what a person expects to already be there**: dragging an
instance into the browser makes a prefab of it and dragging one out places it;
`Del`, `F2`, `Ctrl+D`, `Ctrl+S`; a clipboard on `Ctrl+C`/`X`/`V` and
`Ctrl+Shift+V` that holds TEXT rather than ids — which is what lets a copy
survive the delete, the scene load or the stamp session that happens between it
and the paste.

## Three reversals, and each was right when it was written

This is the milestone's real finding and it is worth more than any feature in
it.

| Reversed | By | Why the first one was reasonable |
|---|---|---|
| 0048 — a Script is created as a FILE | 0050 — its source is a property | It was faithful to ADR 0047 and to the IDL, and it shipped. What it could not do is put a script in a prefab, copy it with the thing it belongs to, or keep it in a library. |
| 0049 — editing breaks the link | 0051 — editing is an override | It was written from the human's own words in 0048. Break-on-edit says a prefab is a starting point; the human meant a definition. |
| 0052 — content gains a tree of instances | 0052, the same afternoon | It was built, tested and shipped before the human asked whether Unity and Unreal have two contents. They do not. |

**Each was corrected by the same person who asked for it, a day or an hour after
having it in their hands.** That is not a process failure — it is the process
working at the only speed it can. What made it cheap was that every one of them
was written down first: a decision in a file is a decision somebody can argue
with, and all three arguments took one sentence.

**And the third one is mine rather than theirs.** The question I asked put "a
file per thing (like Unity and Unreal)" and "a single tree of instances" side by
side as though they were two shapes of one idea, with the second option's own
description listing its costs. A person choosing between two things they have
not built yet is choosing on the framing. **An option whose description lists
its costs is one to argue against, not one to offer neutrally.**

## Gate Record

**SIGNED OFF 2026-08-23.** The human specified E3 message by message while using
it, reported four of its defects by doing so, approved the close, and asked for
one last item — the stamp badge — before it.

Closing run, on the reference machine:

```
  ok    docs (12.8 s)
  ok    luau (10.2 s)
  ok    format (10.7 s)
  ok    windows (62.2 s)
  ok    linux (79 s)
  ok    shipping (42.4 s)
green (macOS is Tier-3 and only CI can build it)
```

| Claim | Answer |
|---|---|
| A stamp round-trips, and a scene holds a mark rather than a copy | **Pass.** `scene_file_tests.cpp`, four cases: the round trip, the collapse to a mark, changing the stamp changing every instance, and a scene naming a stamp nobody can supply still opening. Break-verified — with the collapse removed, three of the four fail. |
| An edit is an override and the link survives it | **Pass.** Three more: the override written and read back, the source moving everything except where an instance said otherwise, and a structural change written in full and unlinked. |
| Make, place, break, and open onto a stage | **Pass.** `editor_tests.cpp`: the file written and the subject converted, a stamp of a stamp refused, one undo for a whole placement, the stage built with the game's world untouched — the test holds ids from before and checks them after, including one it retires on the way in. |
| A script is an instance, and a module is required | **Pass.** `script_environment.spec.luau` asserts the new model where it asserted the old one; `instance_construction.spec.luau` makes both classes; a module's failure is cached rather than retried. |
| The clipboard survives what happens between a copy and a paste | **Pass.** `editor_tests.cpp` deletes the original and pastes it back. |
| The badge's geometry lives in the theme | **Pass.** `debug_overlay_tests.cpp`, on a real device: the two ids are in the staged atlas, a theme with no block gets the documented defaults, and one that overrides all three gets all three. |
| `scripts/localgate.ps1` green on every stage | **Pass**, above. |
| A badge over `class.Folder` at 16 px is legible on both panels | **PENDING — a person at a window.** The knockout is what the claim rests on and it is drawn; whether it reads is a picture. |
| A human uses it and says whether it works | **Pass by construction.** E3 was specified while being used, and four of its defects came from that. |

**Two limits, stated rather than discovered.** The ImGui shell cannot render
headlessly and SDL does not accept injected input, so there is no automated path
to a picture of this editor — every visual claim rests on the human looking.
And `openworld_soak` is still quarantined (D066).

## What E3 does not have

- **A prefab whose source is another prefab** — a variant. Making one is refused
  outright rather than half-answered: which level an override belongs to has no
  answer yet (ADR 0049, ADR 0051).
- **A `content.Stamp` drawing.** A `.stamp.json` has a `ContentKind` of its own
  and wears `class.Model` as a stand-in, with a comment saying so.
- **`src/scripts` is still the mount**, and every example in this repository is
  built on it. Whether it goes away is ADR 0050's open question.
- **Nothing in `content/` is reachable from a script.** There is no global for
  it, deliberately, until somebody needs one — a name on the global list is the
  hardest thing in this API to take back.
