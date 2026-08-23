# 0048 — Content is the source, an instance is a link to it, and editing breaks the link

- Status: accepted
- Date: 2026-08-23
- Extends: 0047 (the world is data and scripts are behaviour)

## Context

ADR 0047 settled where a project's world is written down: a scene under
`content/`, loaded before the scripts run. What it did not settle is the
relationship between the two halves of a project's data — the things in
`content/` and the things in `Workspace` — and E2 ran into the gap twice in one
session.

The first time was a question with a factual answer: **why can a `Script` not be
created?** Because `Script` is `NotCreatable` in the IDL, and the IDL says why —
"a Script exists because a file under `src/scripts` does". The answer is correct
for the runtime and useless to a person in an editor, who has just been told that
the one thing every other engine lets them make from a menu is the one thing this
one does not.

The second was a design stated by the human in their own words, and it is the
model every mainstream engine has:

> vamos ter coisas na workspace e vamos ter coisas no content … coisas do content
> eu devo conseguir instanciar para a workspace … isso não quer dizer que eu não
> consiga criar uma coisa diretamente na workspace … a coisa na workspace quando
> vinda de um content [está] linkada; se eu alterar a da workspace o link perde e
> ele vira uma nova instância

That is Unity's prefab, and it is Unreal's blueprint instance, and it is worth
writing down before any of it is built — because a link that is invented while a
manipulator is being wired is a link designed twice.

## Decision

**`content/` holds SOURCES. `Workspace` holds a world. An instance in the world
may be a LINK to a source, and it stops being one the moment somebody changes
it.**

Four rules, and they are the whole of it.

1. **A source is a file under `content/`.** A scene is one; a prefab is one; a
   mesh and a texture already are. The kind is the file's, not the tree's.

2. **Instantiating a source into the world makes a linked instance.** The
   instance knows which source it came from. Nothing is copied that does not have
   to be: what the instance carries is the link and whatever it overrides.

3. **Editing a linked instance breaks its link, and it becomes its own thing.**
   Not "an override list that grows forever" and not "a change that propagates
   back to the file" — the first is a mechanism nobody can predict and the second
   is an edit that silently changes every other copy. One edit, one unlinked
   instance, and the person can see which of the two it is.

4. **Creating directly in the world stays first-class.** A `Part` made with the
   plus is a `Part`, not a prefab with no source. Code-first does not die (ADR
   0047 said so of scripts and it is said again here of instances).

**And a `Script` is created the way every other engine creates one: as a FILE.**
The IDL's `NotCreatable` is right and stays — `Instance.new("Script")` inside a
sandboxed game VM has no filesystem to put a file on (R4), and a `Script`
instance with no file behind it is a lie the tree tells. What was missing is that
the EDITOR is not the game VM: ADR 0046 put it in the engine binary precisely
because it may touch the disk. So the editor's "new script" writes
`src/scripts/<name>.luau`, and the `Script` appears in the tree because the mount
finds it — which is the rule the IDL states, honoured rather than bypassed.

## Consequences

- **E3 builds this**, and it now has a written model to build against rather than
  one invented while wiring a browser. Its scope gains: a prefab file format (a
  scene of one subtree is the obvious candidate, since `scene_file.h` already
  writes exactly that), an instantiate verb, a link field that survives a save,
  and the break-on-edit rule.
- **The scene format grows a reference.** A linked instance serialises as "this
  source, plus these overrides", not as a full copy — which is what makes a
  prefab worth having and is also the part that has to be got right first.
- **What "editing" means has to be exact**, and it is the one place this ADR
  leaves a decision open: a property write is obviously an edit; whether moving a
  linked instance in the world is one, or whether its transform is always the
  instance's own, is a question for E3 to answer with a person watching. Unity
  answers "the transform is always the instance's".
- **The editor gains a second root in its browser.** `src/` is a project's code
  and `content/` is its data, and an editor that shows only one of them cannot
  offer to make a script. That is a browser change, not a model change.
- **Nothing here weakens R4.** The game VM still cannot write a file. What can is
  the tool, which is a different program that happens to share a binary.
