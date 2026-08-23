# 0051 — A prefab is inherited, an edit is an override, and a copy is the other thing

- Status: accepted
- Date: 2026-08-23
- Reverses: 0049's break-on-edit rule
- Extends: 0048, 0049 (stamps)

## Context

ADR 0049 chose **break on edit**: change anything inside a stamped instance
except its own transform and name, and the mark comes off. That was written from
the human's own words in ADR 0048 — "se eu alterar a da workspace o link perde e
ele vira uma nova instância" — and it shipped with a test.

They used it and reversed it, in one sentence that contains the whole model:

> eu posso instanciar aquele prefab ou seja criar um novo prefab que herda do
> prefab anterior más eu posso mudar alguns paremetros do novo sem influenciar o
> anterior tanto que se eu mecher no anterior ele atualiza todas as instancias
> geradaas apartir daquele prefab

and, separately, that both kinds of placement are wanted:

> o prefab pode ser herança ou pode ser copia sacou é basicamente os 2 ao mesmo
> tempo

**Break-on-edit is not a smaller version of this.** It is a different model: it
says a prefab is a starting point, and this says a prefab is a definition. The
second is the one every mainstream engine has, and it is the one that makes forty
lamp posts worth having as forty instances rather than forty copies.

## Decision

### An instance INHERITS from its stamp

A stamped instance is written as its mark plus **what differs**. Everything it
does not override comes from the file, on every load. Change the file and every
instance changes with it, except where an instance has said otherwise.

A property write on an instance is an **override**: it stays on that instance,
it is written under the path inside the stamp it applies to (`""` for the root,
`Lantern.Bulb` for something under it), and it survives the source changing.

### A structural change is not an override

Adding a child inside one instance, or removing one, is not a parameter of that
instance — it is a different thing. A format that recorded it would be inventing
an added-and-removed-object machinery nobody has designed here.

So the save **writes such an instance in full and drops its mark**, and counts
it. Nothing is lost, the world is exactly what it was, and the instance is
simply its own from then on. A refusal at save time would be the alternative,
and a save that refuses is a save that loses work.

### Placing one is two verbs, not one

- **Linked** — it inherits. This is what "instance a prefab" means.
- **A copy** — the stamp made it and has nothing more to do with it.

Both from the browser's menu and both from code:
`Instance.stamp(name)` and `Instance.stamp(name, false)`.

### The diff is done against the stamp itself

"What differs from the source" is a question about two trees, so a save builds
each stamp it needs into a world of its own, once, and walks the pair. There is
no cheaper honest way: comparing serialised text would compare formatting as
well as values, and comparing against a remembered snapshot would be a cache
that goes stale exactly when the file changes.

`StampLibrary` is that, and it is the caller's to construct — `scene` is L3 and
has no filesystem, so the text comes from a `StampSource` the host supplies. A
save with no library writes every stamped instance in full and counts them,
which is what a caller with no content root can honestly do.

## Consequences

- **`Editor::breakStampsFor` and `stampBrokenBy` are gone**, and the test that
  asserted them now asserts the opposite. They were right for one day.
- **Instance-valued properties are never overrides.** The live one names an
  instance in the live world and the reference names one in the stamp's own, so
  the two are not comparable; a reference inside a stamp resolves inside that
  stamp, which is what placing one already guarantees.
- **A save now reads files**, one per distinct stamp. A world with forty lamp
  posts reads one file, not forty — but a world with forty different prefabs
  reads forty, and that is a cost worth measuring before somebody has a thousand.
- **What is not answered here**: a prefab whose source is itself an instance of
  another prefab (a variant). ADR 0049 refuses to make one and that still holds;
  the override machinery would extend to it, and the question of which level an
  override belongs to has no answer yet.
