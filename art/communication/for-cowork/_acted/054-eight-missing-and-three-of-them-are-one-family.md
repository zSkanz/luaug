# 054 — `Material` was asked for, and the check found eight

From the reviewer. Our human asked for one icon. I ran the class list against the
theme the way `052` established, and the set is further behind than that:

```
classes declared  55      icons  43

missing and CONCRETE (need drawings):
  Material              a surface
  Attachment            a named place on a part
  Bone                  an Attachment that follows a joint
  Ragdoll               a character posed by the solver
  FixedConstraint   \
  HingeConstraint    >  the constraint family
  BallSocketConstraint /

missing and ABSTRACT (need nothing -- no instance ever has this className):
  BasePart  BaseScript  PVInstance  UIObject  Constraint  DataModel
```

Seven drawings, not one. The engine grew a whole physics-joint family and the
set did not follow.

## The three constraints are a family, so ONE chat

`FixedConstraint`, `HingeConstraint` and `BallSocketConstraint` all extend
`Constraint`, which is abstract and gets no icon of its own. So the shared base
has no picture — **the three drawings are where "these are all joints" has to be
said**, and that is exactly `028`'s rule:

> When two icons must share anything exact -- a body, a silhouette, a stroke
> weight -- they come from one chat, the second as a follow-up to the first.

Three, in one chat, each a follow-up to the last. What they share is the **two
bodies and the frame between them**; what differs is the freedom that frame has.

**Turn 1** -- Part A, then:

> `FixedConstraint`: two solid rounded squares side by side, touching a short
> **solid bar** that joins them dead centre. Nothing between them moves: the bar
> is one piece with both. Square keyline.

**Turn 2** -- same chat, no Part A:

> Now `HingeConstraint`. **The two squares are identical** to the image you just
> drew -- same size, same corner radius, same positions. Replace the solid bar
> with a **circle** between them, and cut a small hole from the circle's centre.
> That circle is a pin the two turn about. Nothing else moves.

**Turn 3** -- same chat:

> Now `BallSocketConstraint`. **The two squares are identical again.** Replace
> the pinned circle with a **solid ball sitting in a cup**: a filled circle whose
> lower half is wrapped by a thick open arc, like a ball resting in a socket.
> Nothing else moves.

## The other four, drawn on their own

Each is its own subject and shares nothing, so ordinary Part A, one per chat.

| file | keyline | subject |
|---|---|---|
| `Material.png` | Square | a **sphere with a highlight cut from it**: a solid circle with a small round hole in the upper left where a light would catch. The material preview every engine shows is a lit sphere, and a highlight is the one mark that says *surface* rather than *ball* |
| `Attachment.png` | Square | a **small solid square with four short arms** reaching out from the middle of each edge -- a named point on a surface with a frame attached to it. Not a plus: the centre is a filled block and the arms are short, so it reads as a fitting rather than an operator |
| `Bone.png` | Tall | a **bone from a skeleton**: two lobed ends joined by a narrower shaft, the classic two-lobe shape. Every animation tool draws a bone this way and nothing else in our set is lobed |
| `Ragdoll.png` | Tall | a **figure hanging limp**: a round head and a body whose limbs fall straight down rather than standing. It is the pose that says *the solver is holding this*, and it has to be distinguishable from `CharacterBody`, which stands upright |

## Watch for these, and I will measure them

- **`Material` against `PointLight`.** Both are a round shape with a mark. The
  lamp has a stem breaking its outline and this has none, which should carry it
  -- but it is the pair I expect to be closest.
- **`Bone` against `Weld` and `WeldConstraint`.** Those are already two bodies
  and a joiner; a bone is one object with two ends. Different count, and that is
  the defence.
- **`Ragdoll` against `CharacterBody` and `AnimationPlayer`.** Three figures.
  Standing, moving, limp -- and limp is the one the silhouette has to say on its
  own at 16 px.
- **`Attachment` against `action.Add`.** A plus is a plus. The filled centre and
  the short arms are what stop this being one, and if it comes back as a thin
  cross it will read as *add*.

## Everything else stands

`Weld` / `WeldConstraint` and `ImageLabel` / `ImageButton` are still not being
reopened -- `028`'s note, and I still agree. Nothing here touches them.
